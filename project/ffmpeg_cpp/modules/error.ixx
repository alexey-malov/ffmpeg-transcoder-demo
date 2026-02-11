export module ffmpeg.error;
import std;

namespace ffmpeg
{

export enum class ErrDomain : std::uint8_t {
	None = 0,
	FFMpeg,
};

export struct Error final
{
	int code = 0;
	ErrDomain domain = ErrDomain::None;
	const char* where = nullptr;

	constexpr bool Ok() const noexcept
	{
		return domain == ErrDomain::None || code == 0;
	}

	constexpr explicit operator bool() const noexcept
	{
		return !Ok();
	}
};

static_assert(sizeof(Error) <= 16, "Error struct size must not exceed 16 bytes");
static_assert(std::is_trivially_copyable_v<Error>, "Error struct must be trivially copyable");

export inline constexpr Error MakeFFmpegError(int code, const char* where)
{
	return { .code = code, .domain = ErrDomain::FFMpeg, .where = where };
}

export class Exception : public std::exception
{
public:
	explicit Exception(Error error)
		: m_error{ error }
	{
	}

	[[nodiscard]] const char* what() const noexcept override
	{
		return m_error.where != nullptr ? m_error.where : "ffmpeg::Exception";
	}

	[[nodiscard]] const Error& GetError() const noexcept
	{
		return m_error;
	}

private:
	Error m_error;
};

export inline void ThrowIfError(const Error& error)
{
	if (error) [[unlikely]]
	{
		throw Exception{ error };
	}
}

export inline Error ErrorFromFFmpegErrorCode(int code, const char* where) noexcept
{
	return code < 0 ? MakeFFmpegError(code, where) : Error{};
}

export [[noreturn]] void ThrowFFmpegError(int code, const char* where);

export [[noreturn]] void ThrowFFmpegNoMem(const char* where);

export inline void CheckFFmpegError(int code, const char* where)
{
	if (code < 0) [[unlikely]]
	{
		ThrowFFmpegError(code, where);
	}
}

export [[nodiscard]] inline std::expected<void, Error> ExpectedFromFFmpegErrorCode(int code, const char* where) noexcept
{
	if (code < 0) [[unlikely]]
	{
		return std::unexpected(ErrorFromFFmpegErrorCode(code, where));
	}
	return {};
}

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

} // namespace ffmpeg