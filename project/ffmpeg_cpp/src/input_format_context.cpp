module;
#include "avformat.hpp"

module ffmpeg.input_format_context;
import std;

namespace ffmpeg
{

void InputFormatContext::Deleter::operator()(AVFormatContext* ctx) noexcept
{
	avformat_close_input(&ctx);
}

InputFormatContext::InputFormatContext(const char* url, const AVInputFormat* inputFormat,
	AVDictionary** dictionary)
	: m_ctx{ [url, inputFormat, dictionary] {
		AVFormatContext* ctx = nullptr;
		CheckFFmpegError(avformat_open_input(&ctx, url, inputFormat, dictionary), "avformat_open_input");
		return FormatContextPtr{ ctx };
	}() }
{
}

std::expected<void, Error> InputFormatContext::TryFindStreamInfo(AVDictionary** options) noexcept
{
	auto e = ErrorFromFFmpegErrorCode(avformat_find_stream_info(m_ctx.get(), options), "avformat_find_stream_info");
	if (e) return std::unexpected(e);
	return {};
}

std::expected<void, Error> InputFormatContext::TryReadFrame(AVPacket& packet) noexcept
{
	if (const int code = av_read_frame(m_ctx.get(), &packet); code < 0) [[unlikely]] {
		return std::unexpected(MakeFFmpegError(code, "av_read_frame"));
	}
	return {};
}

} // namespace ffmpeg
