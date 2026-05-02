module;

#include <cassert>

export module mm_pipeline.async_stage;

import std;
import mm_pipeline.async_queue;
import mm_pipeline.pipeline_notifier;
import ffmpeg.packet;
import ffmpeg.frame;
import ffmpeg.codec;

namespace mm_pipeline
{

namespace detail
{

class AsyncStageBase
{
protected:
	struct CancelException : public std::exception
	{
		const char* what() const noexcept override
		{
			return "AsyncStage operation cancelled";
		}
	};
	enum class State
	{
		NotStarted,
		Running,
		Finished,
		Stopped,
		Failed,
	};

	explicit AsyncStageBase(PipelineNotifier& notifier)
		: m_pipelineNotifier{ notifier }
	{
	}

	// This class is not intended for polymorphic deletion
	~AsyncStageBase() = default;

	void JoinImpl()
	{
		if (m_workerThread.joinable())
		{
			m_workerThread.join();
		}
	}

	std::stop_token GetWorkerThreadStopToken() const noexcept
	{
		return m_workerThread.get_stop_token();
	}

	void RequestStopImpl() noexcept
	{
		m_workerThread.request_stop();
	}

	void StartImpl()
	{
		State expected = State::NotStarted;
		if (!m_state.compare_exchange_strong(expected, State::Running, std::memory_order::acq_rel))
		{
			throw std::logic_error("AsyncStage can only be started once");
		}

		try
		{
			m_workerThread = std::jthread{ std::bind_front(&AsyncStageBase::WorkerThreadFunc, this) };
		}
		catch (...)
		{
			m_state.store(State::NotStarted, std::memory_order::release);
			throw;
		}
	}

	void RethrowIfFailedImpl()
	{
		switch (m_state.load(std::memory_order_acquire))
		{
		case State::Failed:
			if (m_workerException)
			{
				std::rethrow_exception(m_workerException);
			}
			throw std::runtime_error("AsyncStage failed with unknown error");

		case State::Stopped:
			throw CancelException{};

		default:
			return;
		}
	}

	PipelineNotifier& m_pipelineNotifier;
	std::exception_ptr m_workerException;
	std::atomic<State> m_state = State::NotStarted;

private:
	virtual void WorkerThreadFunc(std::stop_token stoken) = 0;

	std::jthread m_workerThread;
};

} // namespace detail

export template <class Processor, class Input, class Output>
class AsyncStage : private detail::AsyncStageBase
{
public:
	using CancelException = AsyncStageBase::CancelException;

	enum class TryPushResult
	{
		Ok,
		Full,
	};

	AsyncStage(Processor processor, size_t inputCapacity, size_t outputCapacity, PipelineNotifier& notifier)
		: AsyncStageBase{ notifier }
		, m_inputQueue{ inputCapacity }
		, m_outputQueue{ outputCapacity }
		, m_processor{ std::move(processor) }
	{
	}

	AsyncStage(const AsyncStage&) = delete;
	AsyncStage& operator=(const AsyncStage&) = delete;

	~AsyncStage()
	{
		RequestStop();
		Join();
	}

	void Join()
	{
		JoinImpl();
	}

	void Start()
	{
		StartImpl();
	}

	void RequestStop()
	{
		RequestStopImpl();
		m_inputQueue.NotifyAll();
		m_outputQueue.NotifyAll();
		m_pipelineNotifier.Notify();
	}

	void RethrowIfFailed()
	{
		return RethrowIfFailedImpl();
	}

	// On Ok, the item is consumed.
	// On Full, Stopped, or Failed, the item is not consumed and remains valid.
	TryPushResult TryPush(Input&& input)
	{
		if (!input) [[unlikely]]
		{
			throw std::invalid_argument("Empty input");
		}

		switch (m_state.load(std::memory_order_acquire))
		{
		case State::NotStarted:
		case State::Finished:
			throw std::logic_error("AsyncStage is not running");
		case State::Stopped:
			throw CancelException{};
		case State::Failed:
			std::rethrow_exception(m_workerException);
		case State::Running:
			break;
		}

		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("AsyncStage input is closed");
		}

		switch (m_inputQueue.TryPush(std::move(input)))
		{
		case InputQueue::TryPushResult::Ok:
			return TryPushResult::Ok;
		case InputQueue::TryPushResult::Full:
			return TryPushResult::Full;
		case InputQueue::TryPushResult::Closed:
			throw CancelException{};
		}

		std::unreachable();
	}

	void CloseInput()
	{
		if (m_inputClosed) [[unlikely]]
		{
			throw std::logic_error("AsyncStage input is already closed");
		}

		if (m_state.load(std::memory_order_acquire) != State::Running)
		{
			throw std::logic_error("Cannot close AsyncStage input when it is not running");
		}

		m_inputQueue.PushOrWait(Input::Null(), GetWorkerThreadStopToken());
		m_inputClosed = true;
	}

	std::optional<Output> TryPop()
	{
		auto result = m_outputQueue.TryPop();
		if (result)
		{
			return std::move(*result);
		}

		switch (result.error())
		{
		case OutputQueue::TryPopError::Empty:
			return std::nullopt;

		case OutputQueue::TryPopError::Closed:
			break;
		}

		switch (m_state.load(std::memory_order_acquire))
		{
		case State::Finished:
			return Output::Null();
		case State::Stopped:
			throw CancelException{};
		case State::Failed:
			std::rethrow_exception(m_workerException);
		}
		std::unreachable();
	}

	Processor& GetProcessor() noexcept
	{
		return m_processor;
	}

	const Processor& GetProcessor() const noexcept
	{
		return m_processor;
	}

private:
	using InputQueue = AsyncQueue<Input>;
	using OutputQueue = AsyncQueue<Output>;
	using SendResult = ffmpeg::SendResult;
	using ReceiveResult = ffmpeg::ReceiveResult;

	void WorkerThreadFunc(std::stop_token stopToken) override
	{
		try
		{
			std::deque<Input> batch;

			while (true)
			{
				m_inputQueue.PopAllOrWait(batch, stopToken);
				m_pipelineNotifier.Notify();

				for (Input& input : batch)
				{
					const bool eof = !input;

					ProcessInput(input, stopToken);

					if (eof)
					{
						CloseOutput(State::Finished);
						return;
					}
				}
			}
		}
		catch (const CancelException&)
		{
			CloseOutput(State::Stopped);
		}
		catch (const typename InputQueue::Cancelled&)
		{
			CloseOutput(State::Stopped);
		}
		catch (const typename OutputQueue::Cancelled&)
		{
			CloseOutput(State::Stopped);
		}
		catch (...)
		{
			m_workerException = std::current_exception();
			CloseOutput(State::Failed);
		}
	}

	void ProcessInput(const Input& input, std::stop_token stopToken)
	{
		while (m_processor.Send(input) == SendResult::NeedReceive)
		{
			DrainProcessor(stopToken);
		}

		DrainProcessor(stopToken);
	}

	void DrainProcessor(std::stop_token stopToken)
	{
		Output output;

		while (m_processor.Receive(output) == ReceiveResult::Produced)
		{
			m_outputQueue.PushOrWait(std::move(output), stopToken);
			m_pipelineNotifier.Notify();

			output = {};
		}
	}

	void CloseOutput(State state)
	{
		m_state.store(state, std::memory_order_release);
		m_outputQueue.Close();
		m_pipelineNotifier.Notify();
	}

	InputQueue m_inputQueue;
	OutputQueue m_outputQueue;
	Processor m_processor;
	bool m_inputClosed = false;
};

} // namespace mm_pipeline