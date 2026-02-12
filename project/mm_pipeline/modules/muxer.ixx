module;

#include "../../ffmpeg_cpp/src/codec_par.hpp"
#include "../../ffmpeg_cpp/src/avformat.hpp"

export module mm_pipeline.muxer;
import ffmpeg.output_format_context;
import ffmpeg.rational;
import mm_pipeline.error;

namespace mm_pipeline
{

export class Muxer
{
public:
	class TrackId
	{
		friend class Muxer;
		explicit TrackId(int index) noexcept
			: m_index{ index }
		{
		}
		int m_index;
	};

	Muxer(const char* url, const char* fmtName);

	[[nodiscard]] TrackId AddTrack(const AVCodecParameters& codecPar, ffmpeg::Rational outTimeBase);

	void WriteHeader(AVDictionary** options = nullptr);

	void WritePacket(TrackId track, AVPacket& packet, AVRational timeBase);

	void Close();

	[[nodiscard]] AVRational GetTrackTimeBase(TrackId track) const noexcept;

private:
	struct TrackInfo
	{
		AVStream* stream = nullptr;
	};

	struct State
	{
		bool headerWritten = false;
		bool closed = false;
	};

	[[noreturn]] static void ThrowMuxerError(const char* where, int code = ErrorUnknown)
	{
		throw Exception{ MakeError(ErrDomain::Mux, code, where) };
	}

	ffmpeg::OutputFormatContext m_ctx;
	std::vector<TrackInfo> m_tracks;
	State m_state;
};

} // namespace mm_pipeline