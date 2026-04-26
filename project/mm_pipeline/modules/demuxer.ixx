module;

#include "../../ffmpeg_cpp/src/avutil.hpp"
#include "../../ffmpeg_cpp/src/codec_par.hpp"

export module mm_pipeline.demuxer;

import ffmpeg.input_format_context;
import ffmpeg.packet;
import ffmpeg.rational;
import std;
import mm_pipeline.error;

namespace mm_pipeline
{

export struct StreamInfo
{
	int index = -1;
	AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
	const AVCodecParameters* codecPar = nullptr;
	ffmpeg::Rational timeBase;
	ffmpeg::Rational avgFrameRate;
};

export class Demuxer
{
public:
	explicit Demuxer(const char* url);

	Demuxer(const Demuxer&) = delete;
	Demuxer& operator=(const Demuxer&) = delete;

	Demuxer(Demuxer&&) noexcept;
	Demuxer& operator=(Demuxer&&) noexcept;

	~Demuxer();

	// Returns Null packet on EOF
	[[nodiscard]] std::expected<ffmpeg::Packet, Error> TryRead();
	
	[[nodiscard]] ffmpeg::Packet Read()
	{
		return Check(TryRead());
	}

	[[nodiscard]] unsigned int GetStreamCount() const noexcept { return m_ctx->nb_streams; }

	[[nodiscard]] StreamInfo GetStreamInfo(unsigned int index) const
	{
		if (index >= m_ctx->nb_streams)
		{
			throw Exception{ MakeError(ErrDomain::Demux, ErrorUnknown, "mm_pipeline::Demuxer::GetStreamInfo : invalid stream index") };
		}
		const auto& stream = *m_ctx->streams[index];

		// Use avg_frame_rate if valid, otherwise fallback to r_frame_rate
		ffmpeg::Rational frameRate{ stream.avg_frame_rate };
		if (frameRate.Num() <= 0 || frameRate.Den() <= 0)
		{
			frameRate = ffmpeg::Rational{ stream.r_frame_rate };
		}

		return StreamInfo{
			.index = static_cast<int>(index),
			.type = stream.codecpar->codec_type,
			.codecPar = stream.codecpar,
			.timeBase = ffmpeg::Rational{ stream.time_base },
			.avgFrameRate = frameRate,
		};
	}

private:
	ffmpeg::InputFormatContext m_ctx;
};

} // namespace mm_pipeline
