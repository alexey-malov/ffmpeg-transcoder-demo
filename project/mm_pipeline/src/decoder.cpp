module;

#include "../../ffmpeg_cpp/src/error.hpp"
#include "../../ffmpeg_cpp/src/avcodec.hpp"

module mm_pipeline.decoder;

import ffmpeg.error;
import mm_pipeline.error;

namespace mm_pipeline
{
namespace
{
const AVCodec* FindDecoderOrThrow(AVCodecID id)
{
	const AVCodec* codec = avcodec_find_decoder(id);
	if (!codec)
	{
		throw Exception{ MakeError(ErrDomain::Decode, ErrorUnknown, "mm_pipeline::Decoder : avcodec_find_decoder returned nullptr") };
	}
	return codec;
}
} // namespace

Decoder::Decoder(const AVCodecParameters& codecPar, ffmpeg::Rational streamTimeBase)
	: m_ctx{ FindDecoderOrThrow(codecPar.codec_id) }
	, m_streamTimeBase{ streamTimeBase }
{
	if (auto r = m_ctx.TryFromCodecParameters(codecPar); !r) [[unlikely]]
	{
		throw Exception{ MakeError(ErrDomain::Decode, r.error().code, "mm_pipeline::Decoder : TryFromCodecParameters failed") };
	}

	m_ctx->pkt_timebase = streamTimeBase.ToAV();

	if (auto r = m_ctx.TryOpen(nullptr, /* options */ nullptr); !r) [[unlikely]]
	{
		throw Exception{ MakeError(ErrDomain::Decode, r.error().code, "mm_pipeline::Decoder : TryOpen failed") };
	}
}

std::expected<ffmpeg::SendResult, Error> Decoder::TrySend(const ffmpeg::Packet& pkt) noexcept
{
	auto r = m_ctx.TrySendPacket(pkt.get());
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Decode, r.error().code, "mm_pipeline::Decoder::TrySend : TrySendPacket failed"));
	}
	return *r;
}

std::expected<ffmpeg::ReceiveResult, Error> Decoder::TryReceive(ffmpeg::Frame& frame) noexcept
{
	auto r = m_ctx.TryReceiveFrame(*frame);
	if (!r) [[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Decode, r.error().code, "mm_pipeline::Decoder::TryReceive : TryReceiveFrame failed"));
	}
	return *r;
}

} // namespace mm_pipeline
