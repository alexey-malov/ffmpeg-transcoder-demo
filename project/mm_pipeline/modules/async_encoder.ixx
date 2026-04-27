module;

#include <cassert>

export module mm_pipeline.async_encoder;

import std;
import mm_pipeline.encoder;
import mm_pipeline.pipeline_notifier;
import ffmpeg.frame;
import ffmpeg.packet;
import ffmpeg.codec;
import ffmpeg.rational;

namespace mm_pipeline
{

/**
 * This class manages an asynchronous encoding pipeline with a worker thread.
 *
 * Methods of this class except RequestStop() can only be called from the thread
 * that created the AsyncEncoder instance (the "main" thread).
 */
export class AsyncEncoder
{
public:
	using Frame = ffmpeg::Frame;
	using Packet = ffmpeg::Packet;

	struct CancelException : public std::exception
	{
		const char* what() const noexcept override
		{
			return "AsyncEncoder operation cancelled";
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
	AsyncEncoder(Encoder encoder, size_t inputCapacity, size_t outputCapacity,
		PipelineNotifier& pipelineNotifier)
		: m_inputCapacity{ inputCapacity }
		, m_outputCapacity{ outputCapacity }
		, m_pipelineNotifier{pipelineNotifier}
		, m_encoder{ std::move(encoder) }
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

	// Disable copy/move/assignment explicitly
	AsyncEncoder(const AsyncEncoder&) = delete;
	AsyncEncoder& operator=(const AsyncEncoder&) = delete;

	~AsyncEncoder()
	{
		RequestStop();
		Join();
	}

	ffmpeg::Rational GetStreamTimeBase() const noexcept
	{
		return m_encoder.GetStreamTimeBase();
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
			throw std::runtime_error("AsyncEncoder can only be started once");
		}
		try
		{
			m_workerThread = std::jthread{ std::bind_front(&AsyncEncoder::WorkerThreadFunc, this) };
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
		m_inputQueueHasFreeSpace.notify_one(); // Unblock producers waiting to push frames
		m_inputQueueHasFrames.notify_one(); // Unblock worker if waiting for frames
		m_outputQueueHasFreeSpace.notify_one(); // Unblock worker if waiting to send packets
		m_outputQueueHasPackets.notify_one(); // Unblock consumers waiting to pop packets
	}

	void RethrowIfFailed()
	{
		switch (m_state.load(std::memory_order_acquire))
		{
		case State::Failed:
			// If m_state is failed then m_workerException is set.
			if (m_workerException)
			{
				std::rethrow_exception(m_workerException);
			}
			else
			{
				throw std::runtime_error("AsyncEncoder failed with unknown error");
			}
		case State::Stopped:
			throw CancelException{};
		}
	}

	// On Ok, the item is consumed.
	// On Full, Stopped, or Failed, the item is not consumed and remains valid.
	TryPushResult TryPush(Frame&& frame)
	{
		if (!frame) [[unlikely]]
			throw std::invalid_argument("Empty frame");

		switch (m_state.load(std::memory_order::acquire))
		{
		case State::NotStarted:
		case State::Finished:
			[[unlikely]] throw std::logic_error("AsyncEncoder is not running");
		case State::Stopped:
			return TryPushResult::Stopped;
		case State::Failed:
			return TryPushResult::Failed;
		}

		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("Input is closed");
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

		m_inputQueue.push_back(std::move(frame));
		lock.unlock();
		m_inputQueueHasFrames.notify_one();

		UpdateStats(m_inputQueueStats, waitDuration);

		return TryPushResult::Ok;
	}

	// Signal that no more frames will be pushed.
	// The worker will flush the encoder and finish after processing all queued frames.
	void CloseInput()
	{
		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("Input is already closed");
		}
		if (m_state.load(std::memory_order_acquire) != State::Running)
		{
			throw std::runtime_error("Cannot close input when AsyncEncoder is not running");
		}
		std::unique_lock lock{ m_inputQueueMutex };
		// It is ok to add an empty frame without acquiring free space,
		// as it is a signal for the worker to flush and finish.
		m_inputQueue.push_back(Frame::Null()); // Push null frame as a signal to flush and finish
		m_inputClosed = true;
		lock.unlock();
		m_inputQueueHasFrames.notify_one();
	}

	std::expected<Packet, TryPopError> TryPop()
	{
		if (!m_outputQueueBuffer.empty())
		{
			Packet pkt = std::move(m_outputQueueBuffer.front());
			m_outputQueueBuffer.pop_front();
			return pkt;
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

		auto state = m_state.load(std::memory_order::acquire);

		switch (state)
		{
		case State::Finished:
			return Packet::Null(); // EOF
		case State::Stopped:
			return std::unexpected(TryPopError::Stopped);
		case State::Failed:
			return std::unexpected(TryPopError::Failed);
		default:
			break;
		}

		throw std::logic_error("Invalid state");
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
				Frame frame = GetFrameFromInputQueue(stopToken);
				bool isEmptyFrame = !frame;
				EncodeFrame(frame, stopToken);
				if (isEmptyFrame)
				{
					// An empty frame is a signal to flush the encoder and exit.
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
			// m_workerException must be set before changing state to Failed,
			// so that RethrowIfFailed() can reliably check for it.
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

		m_inputQueueHasFreeSpace.notify_one(); // Unblock any waiting producers
		m_outputQueueHasPackets.notify_one(); // Unblock any waiting consumers
	}

	void EncodeFrame(const Frame& frame, const std::stop_token& stopToken)
	{
		while (m_encoder.Send(frame) == SendResult::NeedReceive)
		{
			DrainEncoder(stopToken);
		}
		// After sending a frame, we should try to receive packets until the encoder
		// needs another frame or reaches end of stream.
		DrainEncoder(stopToken);
	}

	void DrainEncoder(const std::stop_token& stopToken)
	{
		Packet pkt;
		while (m_encoder.Receive(pkt) == ReceiveResult::Produced)
		{
			SendPacketToOutputQueue(std::move(pkt), stopToken);
			pkt = {};
		}
	}

	void SendPacketToOutputQueue(Packet&& pkt, const std::stop_token& stopToken)
	{
		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_outputQueueMutex };
		m_outputQueueHasFreeSpace.wait(lock,
			[this, &stopToken] {
				return stopToken.stop_requested() || (m_outputQueue.size() < m_outputCapacity);
			});
		const auto waitDuration = Clock::now() - waitStart;

		if (stopToken.stop_requested()) [[unlikely]]
		{
			throw CancelException{};
		}
		m_outputQueue.push_back(std::move(pkt));
		lock.unlock();
		m_outputQueueHasPackets.notify_one();
		m_pipelineNotifier.Notify();
	}

	Frame GetFrameFromInputQueue(std::stop_token stopToken)
	{
		if (!m_workerInputQueue.empty())
		{
			Frame frame = std::move(m_workerInputQueue.front());
			m_workerInputQueue.pop_front();
			return frame;
		}

		const auto waitStart = Clock::now();
		std::unique_lock lock{ m_inputQueueMutex };
		m_inputQueueHasFrames.wait(lock, [this, stopToken] {
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
		return GetFrameFromInputQueue(stopToken);
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

	// This flag is set to true when CloseInput() is called. It is used to prevent pushing new frames after closing input.
	bool m_inputClosed = false;

	// The encoder is accessed only by the worker thread, so no need for synchronization on it.
	Encoder m_encoder;

	StatsImpl m_inputQueueStats;
	std::deque<Frame> m_workerInputQueue;
	std::mutex m_inputQueueMutex;
	// These variables are protected by m_inputQueueMutex:
	// This queue may contain m_inputCapacity non-empty frames
	// and at most 1 empty frame (as a signal to flush and finish).
	std::deque<Frame> m_inputQueue;
	std::condition_variable m_inputQueueHasFrames;
	std::condition_variable m_inputQueueHasFreeSpace;
	// -------------------------------------

	StatsImpl m_outputQueueStats;
	std::deque<Packet> m_outputQueueBuffer;
	std::mutex m_outputQueueMutex;
	// These variables are protected by m_outputQueueMutex:
	// This queue contains only non-empty packets produced by the encoder.
	// Pop() may additionally return an empty packet as an EOF marker
	// once output is closed and the queue is drained.
	std::deque<Packet> m_outputQueue;
	std::condition_variable m_outputQueueHasPackets;
	std::condition_variable m_outputQueueHasFreeSpace;
	bool m_outputClosed = false;
	// -------------------------------------

	// This field is accessible only when m_state is Failed, and is set only once, so no need for synchronization on it.
	std::exception_ptr m_workerException;

	std::atomic<State> m_state = State::NotStarted;

	std::jthread m_workerThread;
};

} // namespace mm_pipeline