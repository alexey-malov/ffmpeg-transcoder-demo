module;

#include "../src/avcodec.hpp"

module ffmpeg.codec;

import std;
import ffmpeg.error;

namespace ffmpeg
{

namespace
{
std::expected<SendResult, Error> MakeSendResultFromErrorCode(int code, const char* where)
{
	switch (code)
	{
	case 0:
		return SendResult::Accepted;
	case AVERROR(EAGAIN):
		return SendResult::NeedReceive;
	case AVERROR_EOF:
		return SendResult::Flushed;
	default:
		return std::unexpected(MakeFFmpegError(code, where));
	}
}

std::expected<ReceiveResult, Error> MakeReceiveResultFromErrorCode(int code, const char* where)
{
	switch (code)
	{
	case 0:
		return ReceiveResult::Produced;
	case AVERROR(EAGAIN):
		return ReceiveResult::NeedSend;
	case AVERROR_EOF:
		return ReceiveResult::EndOfStream;
	default:
		return std::unexpected(MakeFFmpegError(code, where));
	}
}

} // namespace

CodecContext::CodecContext(const AVCodec* codec)
	: m_ctx{ avcodec_alloc_context3(codec) }
{
	if (!m_ctx)
	{
		ThrowFFmpegNoMem("avcodec_alloc_context3");
	}
}

std::expected<void, Error> CodecContext::TryOpen(const AVCodec* codec, AVDictionary** options) noexcept
{
	return ExpectedFromFFmpegErrorCode(avcodec_open2(ctx(), codec, options), "avcodec_open2");
}

std::expected<void, Error> CodecContext::TryFromCodecParameters(const AVCodecParameters& par) noexcept
{
	return ExpectedFromFFmpegErrorCode(avcodec_parameters_to_context(ctx(), &par), "avcodec_parameters_to_context");
}

std::expected<void, Error> CodecContext::TryToCodecParameters(AVCodecParameters& par) const noexcept
{
	return ExpectedFromFFmpegErrorCode(avcodec_parameters_from_context(&par, ctx()), "avcodec_parameters_from_context");
}

std::expected<SendResult, Error> CodecContext::TrySendPacket(const AVPacket* pkt) noexcept
{
	return MakeSendResultFromErrorCode(avcodec_send_packet(ctx(), pkt), "avcodec_send_packet");
}

std::expected<ReceiveResult, Error> CodecContext::TryReceiveFrame(AVFrame& frame) noexcept
{
	return MakeReceiveResultFromErrorCode(avcodec_receive_frame(ctx(), &frame), "avcodec_receive_frame");
}

std::expected<SendResult, Error> CodecContext::TrySendFrame(const AVFrame* frame) noexcept
{
	return MakeSendResultFromErrorCode(avcodec_send_frame(ctx(), frame), "avcodec_send_frame");
}

std::expected<ReceiveResult, Error> CodecContext::TryReceivePacket(AVPacket& pkt) noexcept
{
	return MakeReceiveResultFromErrorCode(avcodec_receive_packet(ctx(), &pkt), "avcodec_receive_packet");
}

} // namespace ffmpeg
