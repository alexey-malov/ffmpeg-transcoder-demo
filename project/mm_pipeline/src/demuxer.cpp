module;
#include "../../ffmpeg_cpp/src/error.hpp"
module mm_pipeline.demuxer;

import mm_pipeline.error;
import ffmpeg.error;

namespace mm_pipeline
{

Demuxer::Demuxer(const char* url)
try
	: m_ctx{ url, /* inputFormat= */ nullptr, /* dictionary= */ nullptr }
{
	m_ctx.FindStreamInfo();
}
catch (const ffmpeg::Exception& e)
{
	std::throw_with_nested(
		Exception{
			MakeError(ErrDomain::Demux, e.GetError().code, "mm_pipeline::Demuxer : failed to open input") });
}
catch (...)
{
	std::throw_with_nested(
		Exception{
			MakeError(ErrDomain::Demux, ErrorUnknown, "mm_pipeline::Demuxer: failed to open input (unknown error)") });
}

Demuxer::Demuxer(Demuxer&&) noexcept = default;
Demuxer& Demuxer::operator=(Demuxer&&) noexcept = default;
Demuxer::~Demuxer() = default;

std::expected<DemuxOutput, Error> Demuxer::TryRead()
{
	ffmpeg::Packet packet;

	if (auto r = m_ctx.TryReadFrame(*packet); !r) [[unlikely]]
	{
		const auto err = r.error();
		if (err.code == AVERROR_EOF)
		{
			return DemuxOutput{ EndOfStream{} };
		}
		return std::unexpected(
			MakeError(ErrDomain::Demux, err.code, "mm_pipeline::Demuxer::TryRead : av_read_frame failed"));
	}

	if (packet->stream_index < 0 || packet->stream_index >= static_cast<int>(m_ctx->nb_streams))
		[[unlikely]]
	{
		return std::unexpected(MakeError(ErrDomain::Demux, ErrorUnknown,
			"mm_pipeline::Demuxer::TryRead : invalid stream index in packet"));
	}

	return std::move(packet);
}

} // namespace mm_pipeline
