module;

#include <cassert>

export module mm_pipeline.async_decoder;

import std;
import mm_pipeline.decoder;
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

	AsyncDecoder(Decoder decoder, size_t inputCapacity, size_t outputCapacity)
		: m_inputCapacity{ inputCapacity }
		, m_outputCapacity{ outputCapacity }
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

	void Push(Packet&& pkt)
	{
		if (m_inputClosed)
		{
			throw std::logic_error("Cannot push packets after input is closed");
		}
		if (!pkt)
		{
			throw std::invalid_argument("Cannot push an empty packet. To close stream use CloseInput method");
		}

		std::unique_lock lock{ m_inputQueueMutex };
		m_inputQueueHasFreeSpace.wait(lock, [this] {
			return (m_state.load(std::memory_order::acquire) != State::Running)
				|| (m_inputQueue.size() < m_inputCapacity);
		});

		if (m_state.load(std::memory_order::acquire) != State::Running)
		{
			throw std::runtime_error("Cannot push packets when AsyncDecoder is not running");
		}

		m_inputQueue.push_back(std::move(pkt));
		lock.unlock();
		m_inputQueueHasPackets.notify_one();
	}

	// Signal that no more packets will be pushed.
	// The worker will flush the decoder and finish after processing all queued packets.
	void CloseInput()
	{
		if (m_inputClosed)
		{
			throw std::logic_error("Input is already closed");
		}
		if (m_state.load(std::memory_order_acquire) != State::Running)
		{
			throw std::runtime_error("Cannot close input when AsyncDecoder is not running");
		}

		std::unique_lock lock{ m_inputQueueMutex };
		// It is ok to add an empty packet without acquiring free space,
		// as it is a signal for the worker to flush and finish.
		m_inputQueue.push_back(Packet{});
		m_inputClosed = true;
		lock.unlock();
		m_inputQueueHasPackets.notify_one();
	}

	// Pop a frame from the output queue. Blocks if no frames are available.
	// When the decoder finishes and the output queue is drained,
	// returns an empty frame to signal end of stream.
	// Subsequent calls after EOF also return an empty frame.
	Frame Pop()
	{
		std::unique_lock lock{ m_outputQueueMutex };
		m_outputQueueHasFrames.wait(lock, [this] {
			return m_outputClosed || !m_outputQueue.empty();
		});

		if (!m_outputQueue.empty())
		{
			Frame frame = std::move(m_outputQueue.front());
			m_outputQueue.pop_front();
			lock.unlock();
			m_outputQueueHasFreeSpace.notify_one();
			return frame;
		}

		if (m_state.load(std::memory_order_acquire) == State::Finished)
		{
			return Frame{}; // EOF
		}

		RethrowIfFailed();
		throw std::logic_error("Unknown state");
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
			std::unique_lock lk{ m_outputQueueMutex };
			m_outputClosed = true;
		}

		m_state.store(state, std::memory_order::release);
		m_inputQueueHasFreeSpace.notify_one();
		m_outputQueueHasFrames.notify_one();
	}

	void DecodePacket(const Packet& pkt, const std::stop_token& stopToken)
	{
		while (true)
		{
			auto sendResult = m_decoder.TrySend(pkt);
			if (!sendResult)
			{
				throw std::runtime_error(std::format("Decoder TrySend error: {}", sendResult.error().where));
			}

			if (*sendResult == SendResult::Accepted || *sendResult == SendResult::Flushed)
			{
				break;
			}

			DrainDecoder(stopToken);
		}

		// After sending a packet, try to receive frames until the decoder
		// needs another packet or reaches end of stream.
		DrainDecoder(stopToken);
	}

	void DrainDecoder(const std::stop_token& stopToken)
	{
		while (true)
		{
			Frame frame;
			auto recvResult = m_decoder.TryReceive(frame);
			if (!recvResult)
			{
				throw std::runtime_error(std::format("Decoder drain TryReceive error: {}", recvResult.error().where));
			}

			if (*recvResult == ReceiveResult::Produced)
			{
				SendFrameToOutputQueue(std::move(frame), stopToken);
			}
			else if (*recvResult == ReceiveResult::EndOfStream || *recvResult == ReceiveResult::NeedSend)
			{
				break;
			}
		}
	}

	void SendFrameToOutputQueue(Frame&& frame, const std::stop_token& stopToken)
	{
		std::unique_lock lock{ m_outputQueueMutex };
		m_outputQueueHasFreeSpace.wait(lock, [this, &stopToken] {
			return stopToken.stop_requested() || (m_outputQueue.size() < m_outputCapacity);
		});

		if (stopToken.stop_requested())
		{
			throw CancelException{};
		}

		m_outputQueue.push_back(std::move(frame));
		lock.unlock();
		m_outputQueueHasFrames.notify_one();
	}

	Packet GetPacketFromInputQueue(std::stop_token stopToken)
	{
		std::unique_lock lock{ m_inputQueueMutex };
		m_inputQueueHasPackets.wait(lock, [this, stopToken] {
			return stopToken.stop_requested() || !m_inputQueue.empty();
		});

		if (stopToken.stop_requested())
		{
			throw CancelException{};
		}

		Packet pkt = std::move(m_inputQueue.front());
		m_inputQueue.pop_front();
		lock.unlock();
		m_inputQueueHasFreeSpace.notify_one();
		return pkt;
	}

private:
	size_t m_inputCapacity;
	size_t m_outputCapacity;

	// This flag is set to true when CloseInput() is called.
	// It is used to prevent pushing new packets after closing input.
	bool m_inputClosed = false;

	// The decoder is accessed only by the worker thread.
	Decoder m_decoder;

	std::mutex m_inputQueueMutex;
	// This queue may contain m_inputCapacity non-empty packets
	// and at most 1 empty packet (as a signal to flush and finish).
	std::deque<Packet> m_inputQueue;
	std::condition_variable m_inputQueueHasPackets;
	std::condition_variable m_inputQueueHasFreeSpace;

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