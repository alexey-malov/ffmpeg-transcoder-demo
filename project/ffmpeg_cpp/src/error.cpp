module;

#include "error.hpp"

module ffmpeg.error;

namespace ffmpeg
{

void ThrowFFmpegNoMem(const char* where)
{
	ThrowFFmpegError(AVERROR(ENOMEM), where);
}

void ThrowFFmpegError(int code, const char* where)
{
	throw Exception{ MakeFFmpegError(code, where) };
}

} // namespace ffmpeg