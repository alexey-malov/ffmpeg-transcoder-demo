export module mm_pipeline.error;

import std;

namespace mm_pipeline
{

export enum class ErrDomain : std::uint8_t {
	None = 0,
	Pipeline,
	Demux,
	Decode,
	Encode,
	Mux,
};

export constexpr int ErrorUnknown = std::numeric_limits<int>::min();

export struct Error final
{
	int code = 0; // можно трактовать как native_code (AVERROR/errno/...)
	ErrDomain domain = ErrDomain::None;
	const char* where = nullptr; // статическая строка

	constexpr bool Ok() const noexcept
	{
		return domain == ErrDomain::None || code == 0;
	}
	constexpr explicit operator bool() const noexcept { return !Ok(); }
};

static_assert(sizeof(Error) <= 16);
static_assert(std::is_trivially_copyable_v<Error>);

export inline constexpr Error MakeError(ErrDomain domain, int code, const char* where)
{
	return { .code = code, .domain = domain, .where = where };
}

export class Exception final : public std::exception
{
public:
	explicit Exception(Error error)
		: m_error{ error }
	{
	}

	[[nodiscard]] const char* what() const noexcept override
	{
		return m_error.where != nullptr ? m_error.where : "mm_pipeline::Exception";
	}

	[[nodiscard]] const Error& GetError() const noexcept
	{
		return m_error;
	}

private:
	Error m_error;
};

namespace detail
{
template <class E>
concept ExpectedWithError = requires(E e) {
	// expected-like API
	{ bool(e) } -> std::convertible_to<bool>;
	{ e.error() } -> std::same_as<Error&>; // для const объектов
	typename std::remove_reference_t<E>::value_type;
} && std::same_as<typename std::remove_reference_t<E>::error_type, Error>;

} // namespace detail

export template <class E>
	requires detail::ExpectedWithError<E>
decltype(auto) Check(E&& exp)
{
	using Exp = std::remove_reference_t<E>;
	using T = typename Exp::value_type;

	if (!exp)
	{
		throw Exception{ exp.error() };
	}

	if constexpr (std::is_void_v<T>)
	{
		return;
	}
	else
	{
		return *std::forward<E>(exp);
	}
}

} // namespace mm_pipeline
