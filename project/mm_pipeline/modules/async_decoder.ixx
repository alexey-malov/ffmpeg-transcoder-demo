module;

#include <cassert>

export module mm_pipeline.async_decoder;

import std;
import mm_pipeline.decoder;
import mm_pipeline.pipeline_notifier;
import ffmpeg.frame;
import ffmpeg.packet;
import ffmpeg.codec;

namespace mm_pipeline
{

/**
 * This class manages an asynchronous decoding pipeline with a worker thread.
 *
 * Methods of this class except RequestStop() can only be called from the thread
 * that created the AsyncDecoder instance (the "main" thread).
 */
export class AsyncDecoder
{
public:
	using Frame = ffmpeg::Frame;
	using Packet = ffmpeg::Packet;

	struct CancelException : public std::exception
	{
		const char* what() const noexcept override
		{
			return "AsyncDecoder operation cancelled";
		}
	};

	enum class TryPushResult
	{
		Ok,
		Full,
		Stopped,
		Failed
	};

	enum class TryPopError
	{
		Empty,
		Stopped,
		Failed
	};

	using Clock = std::chrono::high_resolution_clock;
	using Duration = Clock::duration;

	struct Stats
	{
		size_t numLockAcquisitions = 0;
		Duration waitDuration = {};
		size_t maxQueueSize = 0;
		size_t numUpdates = 0;
		std::uint64_t numItems = 0;
	};

	AsyncDecoder(Decoder decoder, size_t inputCapacity, size_t outputCapacity,
		PipelineNotifier& pipelineNotifier)
		: m_inputCapacity{ inputCapacity }
		, m_outputCapacity{ outputCapacity }
		, m_pipelineNotifier{ pipelineNotifier }
		, m_decoder{ std::move(decoder) }
	{
		if (inputCapacity == 0)
		{
			throw std::invalid_argument("inputCapacity must be greater than 0");
		}
		if (outputCapacity == 0)
		{
			throw std::invalid_argument("outputCapacity must be greater than 0");
		}
	}

	AsyncDecoder(const AsyncDecoder&) = delete;
	AsyncDecoder& operator=(const AsyncDecoder&) = delete;

	~AsyncDecoder()
	{
		RequestStop();
		Join();
	}

	Stats GetInputQueueStats() const
	{
		return GetStats(m_inputQueueStats);
	}

	Stats GetOutputQueueStats() const
	{
		return GetStats(m_outputQueueStats);
	}

	void Start()
	{
		State expected = State::NotStarted;
		if (!m_state.compare_exchange_strong(expected, State::Running, std::memory_order::acq_rel))
		{
			throw std::runtime_error("AsyncDecoder can only be started once");
		}

		try
		{
			m_workerThread = std::jthread([this](std::stop_token st) {
				WorkerThreadFunc(st);
			});
		}
		catch (...)
		{
			m_state.store(State::NotStarted, std::memory_order::release);
			throw;
		}
	}

	void Join()
	{
		if (m_workerThread.joinable())
		{
			m_workerThread.join();
		}
	}

	// This method can be called from any thread to signal the worker to stop as soon as possible.
	void RequestStop()
	{
		m_workerThread.request_stop();

		m_inputQueueHasFreeSpace.notify_one();
		m_inputQueueHasPackets.notify_one();
		m_outputQueueHasFreeSpace.notify_one();
		m_outputQueueHasFrames.notify_one();
	}

	void RethrowIfFailed()
	{
		switch (m_state.load(std::memory_order_acquire))
		{
		case State::Failed:
			if (m_workerException)
			{
				std::rethrow_exception(m_workerException);
			}
			else
			{
				throw std::runtime_error("AsyncDecoder failed with unknown error");
			}
		case State::Stopped:
			throw CancelException{};
		default:
			return;
		}
	}

	TryPushResult TryPush(Packet&& pkt)
	{
		if (!pkt) [[unlikely]]
		{
			throw std::invalid_argument("Cannot push an empty packet. To close stream use CloseInput method");
		}

		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("Cannot push packets after input is closed");
		}

		switch (m_state.load(std::memory_order::acquire))
		{
		case State::NotStarted:
		case State::Finished:
			[[unlikely]] throw std::logic_error("Cannot push packets when AsyncDecoder is not running");

		case State::Stopped:
			return TryPushResult::Stopped;

		case State::Failed:
			return TryPushResult::Failed;

		case State::Running:
			break;
		}

		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_inputQueueMutex };
		const auto waitDuration = Clock::now() - waitStart;

		if (m_inputQueue.size() >= m_inputCapacity)
		{
			lock.unlock();
			UpdateStats(m_inputQueueStats, waitDuration);

			return TryPushResult::Full;
		}

		m_inputQueue.push_back(std::move(pkt));
		lock.unlock();
		m_inputQueueHasPackets.notify_one();

		UpdateStats(m_inputQueueStats, waitDuration);

		return TryPushResult::Ok;
	}

	// Signal that no more packets will be pushed.
	// The worker will flush the decoder and finish after processing all queued packets.
	void CloseInput()
	{
		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("Input is already closed");
		}
		if (m_state.load(std::memory_order_acquire) != State::Running) [[unlikely]]
		{
			throw std::runtime_error("Cannot close input when AsyncDecoder is not running");
		}

		std::unique_lock lock{ m_inputQueueMutex };
		// It is ok to add an empty packet without acquiring free space,
		// as it is a signal for the worker to flush and finish.
		m_inputQueue.push_back(Packet::Null());
		m_inputClosed = true;
		lock.unlock();
		m_inputQueueHasPackets.notify_one();
	}

	std::expected<Frame, TryPopError> TryPop()
	{
		if (!m_outputQueueBuffer.empty())
		{
			Frame frame = std::move(m_outputQueueBuffer.front());
			m_outputQueueBuffer.pop_front();
			return frame;
		}

		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_outputQueueMutex };
		const auto waitDuration = Clock::now() - waitStart;

		if (!m_outputQueue.empty())
		{
			m_outputQueueBuffer.swap(m_outputQueue);
			lock.unlock();
			m_outputQueueHasFreeSpace.notify_one();

			UpdateStats(m_outputQueueStats, m_outputQueueBuffer.size(), waitDuration);

			return TryPop();
		}

		if (!m_outputClosed)
		{
			return std::unexpected(TryPopError::Empty);
		}

		lock.unlock();

		switch (m_state.load(std::memory_order::acquire))
		{
		case State::Finished:
			return Frame::Null(); // EOF

		case State::Stopped:
			return std::unexpected(TryPopError::Stopped);

		case State::Failed:
			return std::unexpected(TryPopError::Failed);

		case State::NotStarted:
		case State::Running:
			break;
		}

		throw std::logic_error("AsyncDecoder::TryPop : invalid state");
	}

private:
	enum class State
	{
		NotStarted,
		Running,
		Finished,
		Stopped, // Worker terminated due to stop request
		Failed,
	};

	using SendResult = ffmpeg::SendResult;
	using ReceiveResult = ffmpeg::ReceiveResult;

	struct StatsImpl
	{
		std::atomic<size_t> numLockAcquisitions = 0;
		std::atomic<Duration::rep> waitDuration = {};
		size_t maxQueueSize = 0;
		size_t numUpdates = 0;
		std::uint64_t numItems = 0;
	};

	Stats GetStats(const StatsImpl& stats) const
	{
		if (m_state != State::Stopped && m_state != State::Failed && m_state != State::Finished)
		{
			throw std::logic_error("You can't get stats now");
		}

		return Stats{
			.numLockAcquisitions = stats.numLockAcquisitions.load(std::memory_order_relaxed),
			.waitDuration = Duration{ stats.waitDuration.load(std::memory_order_relaxed) },
			.maxQueueSize = stats.maxQueueSize,
			.numUpdates = stats.numUpdates,
			.numItems = stats.numItems,
		};
	}

	void WorkerThreadFunc(std::stop_token stopToken)
	{
		try
		{
			while (true)
			{
				Packet pkt = GetPacketFromInputQueue(stopToken);
				const bool isEmptyPacket = !pkt;

				DecodePacket(pkt, stopToken);

				if (isEmptyPacket)
				{
					// Empty packet is a signal to flush the decoder and exit.
					break;
				}
			}

			CloseOutput(State::Finished);
		}
		catch (const CancelException&)
		{
			CloseOutput(State::Stopped);
		}
		catch (...)
		{
			m_workerException = std::current_exception();
			CloseOutput(State::Failed);
		}
	}

	void CloseOutput(State state)
	{
		{
			std::lock_guard lk{ m_outputQueueMutex };
			// Publish m_outputClosed and m_state under the same mutex to ensure
			// they are observed consistently by consumers.
			m_outputClosed = true;
			m_state.store(state, std::memory_order::release);
		}

		m_inputQueueHasFreeSpace.notify_one();
		m_outputQueueHasFrames.notify_one();
	}

	void DecodePacket(const Packet& pkt, const std::stop_token& stopToken)
	{
		while (m_decoder.Send(pkt) == SendResult::NeedReceive)
		{
			DrainDecoder(stopToken);
		}

		// After sending a packet, try to receive frames until the decoder
		// needs another packet or reaches end of stream.
		DrainDecoder(stopToken);
	}

	void DrainDecoder(const std::stop_token& stopToken)
	{
		Frame frame;
		while (m_decoder.Receive(frame) == ReceiveResult::Produced)
		{
			SendFrameToOutputQueue(std::move(frame), stopToken);
			frame = {};
		}
	}

	void SendFrameToOutputQueue(Frame&& frame, const std::stop_token& stopToken)
	{
		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_outputQueueMutex };
		m_outputQueueHasFreeSpace.wait(lock, [this, &stopToken] {
			return stopToken.stop_requested() || (m_outputQueue.size() < m_outputCapacity);
		});
		const auto waitDuration = Clock::now() - waitStart;

		if (stopToken.stop_requested())
		{
			throw CancelException{};
		}

		m_outputQueue.push_back(std::move(frame));
		lock.unlock();
		m_outputQueueHasFrames.notify_one();
		m_pipelineNotifier.Notify();

		UpdateStats(m_outputQueueStats, waitDuration);
	}

	Packet GetPacketFromInputQueue(std::stop_token stopToken)
	{
		if (!m_workerInputQueue.empty())
		{
			Packet pkt = std::move(m_workerInputQueue.front());
			m_workerInputQueue.pop_front();
			return pkt;
		}

		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_inputQueueMutex };
		m_inputQueueHasPackets.wait(lock, [this, stopToken] {
			return stopToken.stop_requested() || !m_inputQueue.empty();
		});
		const auto waitDuration = Clock::now() - waitStart;

		if (stopToken.stop_requested())
		{
			throw CancelException{};
		}
		m_workerInputQueue.swap(m_inputQueue);
		lock.unlock();
		m_inputQueueHasFreeSpace.notify_one();
		m_pipelineNotifier.Notify();

		UpdateStats(m_inputQueueStats, m_workerInputQueue.size(), waitDuration);

		assert(!m_workerInputQueue.empty());
		return GetPacketFromInputQueue(stopToken);
	}

	void UpdateStats(StatsImpl& stats, size_t queueSize, Duration waitDuration)
	{
		stats.maxQueueSize = std::max(stats.maxQueueSize, queueSize);
		stats.numItems += queueSize;
		++stats.numUpdates;
		UpdateStats(stats, waitDuration);
	}

	void UpdateStats(StatsImpl& stats, Duration waitDuration)
	{
		stats.waitDuration.fetch_add(waitDuration.count(), std::memory_order_relaxed);
		stats.numLockAcquisitions.fetch_add(1, std::memory_order_relaxed);
	}

	size_t m_inputCapacity;
	size_t m_outputCapacity;

	PipelineNotifier& m_pipelineNotifier;

	// This flag is set to true when CloseInput() is called.
	// It is used to prevent pushing new packets after closing input.
	bool m_inputClosed = false;

	// The decoder is accessed only by the worker thread.
	Decoder m_decoder;

	StatsImpl m_inputQueueStats;
	std::deque<Packet> m_workerInputQueue;
	std::mutex m_inputQueueMutex;
	// This queue may contain m_inputCapacity non-empty packets
	// and at most 1 empty packet (as a signal to flush and finish).
	std::deque<Packet> m_inputQueue;
	std::condition_variable m_inputQueueHasPackets;
	std::condition_variable m_inputQueueHasFreeSpace;

	StatsImpl m_outputQueueStats;
	std::deque<Frame> m_outputQueueBuffer;
	std::mutex m_outputQueueMutex;
	// This queue contains only non-empty frames produced by the decoder.
	// Pop() may additionally return an empty frame as an EOF marker
	// once output is closed and the queue is drained.
	std::deque<Frame> m_outputQueue;
	std::condition_variable m_outputQueueHasFrames;
	std::condition_variable m_outputQueueHasFreeSpace;
	bool m_outputClosed = false;

	// Written once by worker before publishing Failed state.
	std::exception_ptr m_workerException;

	std::atomic<State> m_state = State::NotStarted;
	std::jthread m_workerThread;
};

} // namespace mm_pipeline