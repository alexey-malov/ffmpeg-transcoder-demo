module;

#include "error.hpp"

module ffmpeg.error;

import std;

namespace ffmpeg
{

namespace
{
class FFmpegErrorCategory final : public std::error_category
{
public:
	const char* name() const noexcept override { return "ffmpeg"; }

	std::string message(int ev) const override
	{
		std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
		if (av_strerror(ev, buffer.data(), buffer.size()) == 0)
		{
			return buffer.data();
		}
		return "Unknown FFmpeg error";
	}
};

} // namespace

const std::error_category& GetFFmpegErrorCategory() noexcept
{
	static const FFmpegErrorCategory errorCategory;
	return errorCategory;
}

std::error_code Error::ToErrorCode() const noexcept
{
	return std::error_code(code, GetFFmpegErrorCategory());
}

void ThrowFFmpegNoMem(const char* where)
{
	ThrowError(AVERROR(ENOMEM), where);
}

void ThrowError(int code, const char* where)
{
	throw Exception{ MakeError(code, where) };
}

} // namespace ffmpeg