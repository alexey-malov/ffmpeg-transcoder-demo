export module mm_pipeline.async_queue;

import std;

namespace mm_pipeline
{

namespace detail
{

class AsyncQueueBase
{
protected:
	explicit AsyncQueueBase(size_t capacity)
		: m_capacity{ capacity }
	{
		if (capacity == 0)
			throw std::invalid_argument("capacity must be > 0");
	}

	AsyncQueueBase(const AsyncQueueBase&) = delete;
	AsyncQueueBase& operator=(const AsyncQueueBase&) = delete;

	void CloseImpl()
	{
		std::lock_guard lock{ m_mutex };
		m_closed = true;

		m_cvNotEmpty.notify_all();
		m_cvNotFull.notify_all();
	}

	[[nodiscard]] bool IsClosedImpl() const noexcept
	{
		std::lock_guard lock{ m_mutex };
		return m_closed;
	}

	void NotifyAllImpl() noexcept
	{
		m_cvNotEmpty.notify_all();
		m_cvNotFull.notify_all();
	}

	const size_t m_capacity;

	mutable std::mutex m_mutex;
	std::condition_variable_any m_cvNotEmpty;
	std::condition_variable_any m_cvNotFull;

	bool m_closed = false;
};
} // namespace detail

export template <typename T>
class AsyncQueue : private detail::AsyncQueueBase
{
public:
	enum class TryPushResult
	{
		Ok,
		Full,
		Closed
	};

	enum class TryPopError
	{
		Empty,
		Closed
	};

	struct Cancelled : std::exception
	{
		const char* what() const noexcept override
		{
			return "AsyncQueue cancelled";
		}
	};

	struct Closed : std::exception
	{
		const char* what() const noexcept override
		{
			return "AsyncQueue closed";
		}
	};

	explicit AsyncQueue(size_t capacity)
		: AsyncQueueBase{ capacity }
	{
	}

	AsyncQueue(const AsyncQueue&) = delete;
	AsyncQueue& operator=(const AsyncQueue&) = delete;

	// =======================
	// Try API (non-blocking)
	// =======================

	TryPushResult TryPush(const T& value, bool ignoreCapacity = false)
	{
		return TryPushImpl(value, ignoreCapacity);
	}

	TryPushResult TryPush(T&& value, bool ignoreCapacity = false)
	{
		return TryPushImpl(std::move(value), ignoreCapacity);
	}

	std::expected<T, TryPopError> TryPop()
	{
		std::unique_lock lock{ m_mutex };

		if (!m_queue.empty())
		{
			T value = std::move(m_queue.front());
			m_queue.pop_front();

			lock.unlock();
			m_cvNotFull.notify_one();

			return value;
		}

		if (m_closed)
			return std::unexpected(TryPopError::Closed);

		return std::unexpected(TryPopError::Empty);
	}

	// =======================
	// Blocking API (single item)
	// =======================

	void PushOrWait(T value, std::stop_token st)
	{
		std::unique_lock lock{ m_mutex };

		m_cvNotFull.wait(lock, st, [this] {
			return m_closed || m_queue.size() < m_capacity;
		});

		if (st.stop_requested())
			throw Cancelled{};

		if (m_closed)
			throw std::logic_error("Push on closed AsyncQueue");

		m_queue.push_back(std::move(value));

		lock.unlock();
		m_cvNotEmpty.notify_one();
	}

	T PopOrWait(std::stop_token st)
	{
		std::unique_lock lock{ m_mutex };

		m_cvNotEmpty.wait(lock, st, [this] {
			return m_closed || !m_queue.empty();
		});

		if (st.stop_requested())
			throw Cancelled{};

		if (!m_queue.empty())
		{
			T value = std::move(m_queue.front());
			m_queue.pop_front();

			lock.unlock();
			m_cvNotFull.notify_one();

			return value;
		}

		throw Closed{};
	}

	// =======================
	// Batching API
	// =======================

	void PopAllOrWait(std::deque<T>& out, std::stop_token st)
	{
		out.clear();

		std::unique_lock lock{ m_mutex };

		m_cvNotEmpty.wait(lock, st, [this] {
			return m_closed || !m_queue.empty();
		});

		if (st.stop_requested())
			throw Cancelled{};

		if (m_queue.empty() && m_closed)
			throw Closed{};

		out.swap(m_queue);

		lock.unlock();
		m_cvNotFull.notify_all();
	}

	size_t TryPopAll(std::deque<T>& out)
	{
		out.clear();

		std::unique_lock lock{ m_mutex };

		if (m_queue.empty())
			return 0;

		const size_t n = m_queue.size();

		out.swap(m_queue);

		lock.unlock();
		m_cvNotFull.notify_all();

		return n;
	}

	size_t PopUpToOrWait(std::deque<T>& out, size_t maxItems, std::stop_token st)
	{
		if (maxItems == 0) [[unlikely]]
			throw std::invalid_argument("maxItems must be greater than 0");

		out.clear();

		std::unique_lock lock{ m_mutex };

		m_cvNotEmpty.wait(lock, st, [this] {
			return m_closed || !m_queue.empty();
		});

		if (st.stop_requested())
			throw Cancelled{};

		if (m_queue.empty() && m_closed)
			throw Closed{};

		const size_t count = std::min(maxItems, m_queue.size());

		for (size_t i = 0; i < count; ++i)
		{
			out.push_back(std::move(m_queue.front()));
			m_queue.pop_front();
		}

		lock.unlock();
		m_cvNotFull.notify_all();

		return count;
	}

	// =======================
	// Lifecycle
	// =======================

	void Close()
	{
		CloseImpl();
	}

	[[nodiscard]] bool IsClosed() const noexcept
	{
		return IsClosedImpl();
	}

	void NotifyAll() noexcept
	{
		NotifyAllImpl();
	}

private:
	template <class U>
	TryPushResult TryPushImpl(U&& value, bool ignoreCapacity)
	{
		std::unique_lock lock{ m_mutex };

		if (m_closed)
			return TryPushResult::Closed;

		if (!ignoreCapacity && m_queue.size() >= m_capacity)
			return TryPushResult::Full;

		m_queue.emplace_back(std::forward<U>(value));

		lock.unlock();
		m_cvNotEmpty.notify_one();

		return TryPushResult::Ok;
	}

	std::deque<T> m_queue;
};

} // namespace mm_pipeline