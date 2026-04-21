module;

#include "error.hpp"

module ffmpeg.error;

namespace ffmpeg
{

void ThrowFFmpegNoMem(const char* where)
{
	ThrowError(AVERROR(ENOMEM), where);
}

void ThrowError(int code, const char* where)
{
	throw Exception{ MakeError(code, where) };
}

} // namespace ffmpeg