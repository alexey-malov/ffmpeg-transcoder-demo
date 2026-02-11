module;

#include "../../ffmpeg_cpp/src/codec_par.hpp"

export module mm_pipeline.decoder;

import std;
import ffmpeg.codec;
import ffmpeg.frame;
import ffmpeg.packet;
import ffmpeg.rational;
import mm_pipeline.error;

namespace mm_pipeline
{

export class Decoder
{
public:
	Decoder(const AVCodecParameters& codecPar, ffmpeg::Rational streamTimeBase);

	Decoder(const Decoder&) = delete;
	Decoder& operator=(const Decoder&) = delete;

	Decoder(Decoder&&) noexcept = default;
	Decoder& operator=(Decoder&&) noexcept = default;

	~Decoder() = default;

	// pkt == nullptr => flush
	[[nodiscard]] std::expected<ffmpeg::SendResult, Error> TrySend(const ffmpeg::Packet& pkt) noexcept;

	[[nodiscard]] std::expected<ffmpeg::ReceiveResult, Error> TryReceive(ffmpeg::Frame& frame) noexcept;

	[[nodiscard]] ffmpeg::Rational GetStreamTimeBase() const noexcept { return m_streamTimeBase; }

private:
	ffmpeg::CodecContext m_ctx;
	ffmpeg::Rational m_streamTimeBase;
};

} // namespace mm_pipeline
