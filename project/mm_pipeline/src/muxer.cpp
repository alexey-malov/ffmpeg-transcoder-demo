module;

#include "../../ffmpeg_cpp/src/codec_par.hpp"
#include "../../ffmpeg_cpp/src/error.hpp"
#include "../../ffmpeg_cpp/src/rational.hpp"
#include <cassert>

module mm_pipeline.muxer;
import mm_pipeline.error;
import ffmpeg.error;

namespace mm_pipeline
{

Muxer::Muxer(const char* url, const char* fmtName)
try
	: m_ctx{ url, fmtName }
{
	m_ctx.OpenFileIO();
}
catch (const ffmpeg::Exception& e)
{
	std::throw_with_nested(
		Exception{
			MakeError(ErrDomain::Mux, e.GetError().code, "mm_pipeline::Muxer : failed to open output") });
}
catch (...)
{
	std::throw_with_nested(
		Exception{
			MakeError(ErrDomain::Mux, ErrorUnknown, "mm_pipeline::Muxer: failed to open output (unknown error)") });
}

Muxer::TrackId Muxer::AddTrack(const AVCodecParameters& codecPar, ffmpeg::Rational outTimeBase)
{
	auto stExp = m_ctx.TryAddStream();
	if (!stExp) [[unlikely]]
	{
		ThrowMuxerError("mm_pipeline::Muxer::AddTrack : TryAddStream failed", stExp.error().code);
	}

	AVStream* stream = *stExp;

	if (const int result = avcodec_parameters_copy(stream->codecpar, &codecPar); result < 0) [[unlikely]]
	{
		ThrowMuxerError("mm_pipeline::Muxer::AddTrack : avcodec_parameters_copy failed", result);
	}

	stream->time_base = outTimeBase.ToAV();

	const int index = stream->index;
	if (index < 0) [[unlikely]]
	{
		ThrowMuxerError("mm_pipeline::Muxer::AddTrack : avformat_new_stream returned negative index");
	}

	if (const auto i = static_cast<size_t>(index); m_tracks.size() <= i)
	{
		m_tracks.resize(i + 1);
	}
	m_tracks[index] = TrackInfo{ .stream = stream };

	return TrackId{ index };
}

void Muxer::WriteHeader(AVDictionary** options)
{
	if (auto r = m_ctx.TryWriteHeader(options); !r) [[unlikely]]
	{
		ThrowMuxerError("mm_pipeline::Muxer::WriteHeader : TryWriteHeader failed", r.error().code);
	}
	m_state.headerWritten = true;
}

void Muxer::WritePacket(TrackId track, AVPacket& packet, AVRational timeBase)
{
	assert(m_state.headerWritten && "WriteHeader must be called before WritePacket");

	auto& ti = m_tracks.at(track.m_index);
	assert(ti.stream != nullptr && "Invalid track stream pointer");

	packet.stream_index = track.m_index;
	av_packet_rescale_ts(&packet, timeBase, ti.stream->time_base);

	if (auto r = m_ctx.TryInterleavedWritePacket(packet); !r) [[unlikely]]
	{
		ThrowMuxerError("mm_pipeline::Muxer::WritePacket : TryInterleavedWritePacket failed", r.error().code);
	}
}

void Muxer::Close()
{
	assert(m_ctx.get() != nullptr);
	m_ctx.Close();
}

AVRational Muxer::GetTrackTimeBase(TrackId track) const noexcept
{
	const auto& ti = m_tracks.at(track.m_index);
	assert(ti.stream != nullptr && "Invalid track stream pointer");
	return ti.stream->time_base;
}

} // namespace mm_pipeline
