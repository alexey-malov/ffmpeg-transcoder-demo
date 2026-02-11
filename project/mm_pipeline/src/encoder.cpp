module;

#include "../../ffmpeg_cpp/src/error.hpp"
#include "../../ffmpeg_cpp/src/avcodec.hpp"
#include <cassert>

module mm_pipeline.encoder;

import ffmpeg.error;
import mm_pipeline.error;

namespace mm_pipeline
{

Encoder::Encoder(const AVCodec* codec, ffmpeg::Rational streamTimeBase)
	: m_ctx{ codec }
	, m_codec{ codec }
	, m_streamTimeBase{ streamTimeBase }
{
	if (!codec) [[unlikely]]
	{
		throw Exception{ MakeError(ErrDomain::Encode, ErrorUnknown, "mm_pipeline::Encoder : codec is nullptr") };
	}

	// Set the encoder's time base
	m_ctx->time_base = streamTimeBase.ToAV();
}

std::expected<void, Error> Encoder::TryOpen(AVDictionary** options) noexcept
{
	assert(!m_opened && "Encoder::TryOpen called on already opened encoder");
	
	auto r = m_ctx.TryOpen(m_codec, options);
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Encode, r.error().code, "mm_pipeline::Encoder : TryOpen failed"));
	}
	
	m_opened = true;
	return {};
}

std::expected<ffmpeg::SendResult, Error> Encoder::TrySend(const ffmpeg::Frame& frame) noexcept
{
	assert(m_opened && "Encoder::TrySend called on non-opened encoder");
	
	// If frame is empty (!frame), pass nullptr to flush
	const AVFrame* framePtr = frame ? frame.get() : nullptr;
	
	auto r = m_ctx.TrySendFrame(framePtr);
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Encode, r.error().code, "mm_pipeline::Encoder::TrySend : TrySendFrame failed"));
	}
	return *r;
}

std::expected<ffmpeg::ReceiveResult, Error> Encoder::TryReceive(ffmpeg::Packet& pkt) noexcept
{
	assert(m_opened && "Encoder::TryReceive called on non-opened encoder");
	assert(pkt && "Encoder::TryReceive called with empty packet");
	
	auto r = m_ctx.TryReceivePacket(*pkt);
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Encode, r.error().code, "mm_pipeline::Encoder::TryReceive : TryReceivePacket failed"));
	}
	return *r;
}

std::expected<void, Error> Encoder::TryToCodecParameters(AVCodecParameters& out) const noexcept
{
	auto r = m_ctx.TryToCodecParameters(out);
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Encode, r.error().code, "mm_pipeline::Encoder::TryToCodecParameters : TryToCodecParameters failed"));
	}
	return {};
}

} // namespace mm_pipeline
