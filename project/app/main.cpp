import std;
import mm_pipeline.demuxer;
import mm_pipeline.muxer;
import mm_pipeline.decoder;
import ffmpeg.codec;
import ffmpeg.packet;

int main()
{
	using mm_pipeline::Muxer;
	using mm_pipeline::Demuxer;
	using mm_pipeline::Decoder;

	Demuxer demuxer{ R"(C:\videos\Scope Guards in C++.mp4)" };
	Muxer muxer{ R"(C:\videos\output.mp4)", "mp4" };

	struct SrcStreamState
	{
		Muxer::TrackId trackId;
		Decoder decoder;
	};

	std::vector<SrcStreamState> srcStreams;

	{
		for (unsigned i = 0; i < demuxer.GetStreamCount(); ++i)
		{
			auto streamInfo = demuxer.GetStreamInfo(i);
			srcStreams.emplace_back(
				muxer.AddTrack(*streamInfo.codecPar, streamInfo.timeBase),
				Decoder{*streamInfo.codecPar, streamInfo.timeBase}
			);

		}
	}

	muxer.WriteHeader();
	

	while (true)
	{
		auto result = demuxer.TryRead();
		if (!result)
		{
			std::cerr << "Demuxing error: " << result.error().where << "\n";
			break;
		}
		if (std::holds_alternative<mm_pipeline::EndOfStream>(*result))
		{
			std::cout << "End of stream reached.\n";
			break;
		}
		auto& packet = std::get<ffmpeg::Packet>(*result);

		auto& srcStreamInfo = srcStreams[packet->stream_index];

		muxer.WritePacket(srcStreamInfo.trackId, *packet, demuxer.GetStreamInfo(packet->stream_index).timeBase.ToAV());
	}

	muxer.Close();
}
