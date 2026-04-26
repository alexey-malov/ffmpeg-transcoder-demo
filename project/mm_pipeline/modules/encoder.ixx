module;

#include "../../ffmpeg_cpp/src/avcodec.hpp"
#include "../../ffmpeg_cpp/src/codec_par.hpp"

export module mm_pipeline.encoder;

import std;
import ffmpeg.codec;
import ffmpeg.frame;
import ffmpeg.packet;
import ffmpeg.rational;
import mm_pipeline.error;

namespace mm_pipeline
{

export class Encoder
{
public:
	// Construct encoder from an AVCodec* (chosen externally) and a stream time base.
	// Configuration must be external; Encoder itself must not assume audio/video specifics.
	Encoder(const AVCodec* codec, ffmpeg::Rational streamTimeBase);

	Encoder(const Encoder&) = delete;
	Encoder& operator=(const Encoder&) = delete;

	Encoder(Encoder&&) noexcept = default;
	Encoder& operator=(Encoder&&) noexcept = default;

	~Encoder() = default;

	// Must be called after the user configures the codec context (via Context()).
	[[nodiscard]] std::expected<void, Error> TryOpen(AVDictionary** options = nullptr) noexcept;

	// empty frame => flush
	[[nodiscard]] std::expected<ffmpeg::SendResult, Error> TrySend(const ffmpeg::Frame& frame) noexcept;
	[[nodiscard]] ffmpeg::SendResult Send(const ffmpeg::Frame& frame)
	{
		return Check(TrySend(frame));
	}

	// produces encoded packets into pkt (pkt is an RAII wrapper)
	[[nodiscard]] std::expected<ffmpeg::ReceiveResult, Error> TryReceive(ffmpeg::Packet& pkt) noexcept;
	[[nodiscard]] ffmpeg::ReceiveResult Receive(ffmpeg::Packet& pkt)
	{
		return Check(TryReceive(pkt));
	}

	// Export encoder parameters (e.g. for muxer track codecpar).
	[[nodiscard]] std::expected<void, Error> TryToCodecParameters(AVCodecParameters& out) const noexcept;

	[[nodiscard]] ffmpeg::Rational GetStreamTimeBase() const noexcept { return m_streamTimeBase; }

	// Provide access for external configuration, but keep this in mm_pipeline (do not expose AVCodecContext).
	// Returning ffmpeg::CodecContext& is allowed.
	[[nodiscard]] ffmpeg::CodecContext& Context() noexcept { return m_ctx; }
	[[nodiscard]] const ffmpeg::CodecContext& Context() const noexcept { return m_ctx; }

private:
	ffmpeg::CodecContext m_ctx;
	const AVCodec* m_codec = nullptr;
	ffmpeg::Rational m_streamTimeBase;
	bool m_opened = false;
};

} // namespace mm_pipeline
