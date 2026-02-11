module;

#include <cassert>

// unique_handle.ixx
export module ffmpeg.unique_handle;

import std;

namespace ffmpeg
{

namespace detail
{

// Use EBO only when deleter is empty and not final.
template <class D>
inline constexpr bool UseEbo = std::is_empty_v<D> && !std::is_final_v<D>;

// Stores deleter either as an empty base (EBO) or as a data member.
template <class Deleter, bool Ebo>
struct DeleterStorage;

// EBO: deleter as base class
template <class Deleter>
struct DeleterStorage<Deleter, true> : private Deleter
{
	constexpr DeleterStorage() noexcept = default;

	constexpr explicit DeleterStorage(Deleter d) noexcept(std::is_nothrow_move_constructible_v<Deleter>)
		: Deleter(std::move(d))
	{
	}

	constexpr Deleter& get_deleter() noexcept { return *this; }
	constexpr const Deleter& get_deleter() const noexcept { return *this; }
};

// Non-EBO: deleter as a member
template <class Deleter>
struct DeleterStorage<Deleter, false>
{
	constexpr DeleterStorage() noexcept = default;

	constexpr explicit DeleterStorage(Deleter d) noexcept(std::is_nothrow_move_constructible_v<Deleter>)
		: m_deleter(std::move(d))
	{
	}

	constexpr Deleter& get_deleter() noexcept { return m_deleter; }
	constexpr const Deleter& get_deleter() const noexcept { return m_deleter; }

private:
	Deleter m_deleter{};
};

template <class Derived, class T>
class UniqueHandleBase
{
public:
	constexpr UniqueHandleBase() noexcept = default;

	UniqueHandleBase(const UniqueHandleBase&) = delete;
	UniqueHandleBase& operator=(const UniqueHandleBase&) = delete;

	constexpr UniqueHandleBase(UniqueHandleBase&& other) noexcept
		: m_ptr(std::exchange(other.m_ptr, nullptr))
	{
	}

	constexpr UniqueHandleBase& operator=(UniqueHandleBase&& other) noexcept
	{
		if (this != &other) [[likely]]
		{
			reset();
			m_ptr = std::exchange(other.m_ptr, nullptr);
		}
		return *this;
	}

	~UniqueHandleBase() { reset(); }

	[[nodiscard]] constexpr T* get() noexcept { return m_ptr; }
	[[nodiscard]] constexpr const T* get() const noexcept { return m_ptr; }

	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_ptr != nullptr; }

	[[nodiscard]] constexpr T* release() noexcept { return std::exchange(m_ptr, nullptr); }

	constexpr void reset(T* p = nullptr) noexcept
	{
		if (m_ptr) [[likely]]
		{
			derived().get_deleter()(m_ptr);
		}
		m_ptr = p;
	}

	constexpr void swap(Derived& other) noexcept
	{
		using std::swap;
		swap(m_ptr, other.m_ptr);
		swap(derived().get_deleter(), other.get_deleter());
	}

	constexpr T& operator*() noexcept
	{
		assert(m_ptr);
		return *m_ptr;
	}
	constexpr const T& operator*() const noexcept
	{
		assert(m_ptr);
		return *m_ptr;
	}

	constexpr T* operator->() noexcept
	{
		assert(m_ptr);

		return m_ptr;
	}
	constexpr const T* operator->() const noexcept
	{
		assert(m_ptr);
		return m_ptr;
	}

protected:
	constexpr void SetPtr(T* p) noexcept { m_ptr = p; }

private:
	constexpr Derived& derived() noexcept { return static_cast<Derived&>(*this); }
	constexpr const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

private:
	T* m_ptr = nullptr;
};

template <class T, class Deleter>
concept DeleterFor = requires(Deleter d, T* p) {
	{ d(p) } noexcept;
};

} // namespace detail

export template <class T, class Deleter>
	requires detail::DeleterFor<T, Deleter>
class UniqueHandle final
	: private detail::DeleterStorage<Deleter, detail::UseEbo<Deleter>>
	, public detail::UniqueHandleBase<UniqueHandle<T, Deleter>, T>
{
	using Storage = detail::DeleterStorage<Deleter, detail::UseEbo<Deleter>>;
	using Base = detail::UniqueHandleBase<UniqueHandle, T>;

public:
	constexpr UniqueHandle() noexcept = default;

	constexpr explicit UniqueHandle(T* p) noexcept
	{
		Base::SetPtr(p);
	}

	constexpr UniqueHandle(T* p, Deleter d) noexcept(std::is_nothrow_move_constructible_v<Deleter>)
		: Storage(std::move(d))
	{
		Base::SetPtr(p);
	}

	constexpr UniqueHandle(UniqueHandle&& other) noexcept(std::is_nothrow_move_constructible_v<Deleter>)
		: Storage(std::move(other.get_deleter()))
		, Base(std::move(static_cast<Base&>(other)))
	{
	}

	constexpr UniqueHandle& operator=(UniqueHandle&& other) noexcept(std::is_nothrow_move_assignable_v<Deleter>)
	{
		if (this != &other)
		{
			// Ensure current resource released before overwriting deleter/ptr.
			this->reset();
			get_deleter() = std::move(other.get_deleter());
			Base::SetPtr(other.release());
		}
		return *this;
	}

	[[nodiscard]] constexpr Deleter& get_deleter() noexcept { return Storage::get_deleter(); }
	[[nodiscard]] constexpr const Deleter& get_deleter() const noexcept { return Storage::get_deleter(); }

	// Convenience alias: matches std::unique_ptr naming.
	[[nodiscard]] constexpr Deleter& get_deleter_ref() noexcept { return get_deleter(); }
	[[nodiscard]] constexpr const Deleter& get_deleter_ref() const noexcept { return get_deleter(); }
};

} // namespace ffmpeg
