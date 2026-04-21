export module mm_pipeline.transcoder;

import std;
import mm_pipeline.encoder;
import mm_pipeline.decoder;
import mm_pipeline.muxer;
import mm_pipeline.demuxer;
import mm_pipeline.error;
import ffmpeg.packet;
import ffmpeg.frame;

namespace mm_pipeline
{

export class Transcoder
{
public:
	using Frame = ffmpeg::Frame;
	using Packet = ffmpeg::Packet;

	struct BranchConfig
	{
		Decoder& decoder;
		Encoder& encoder;
		int streamIndex = -1;
		Muxer::TrackId track;
	};

	Transcoder(Muxer& muxer, Demuxer& demuxer,
		const BranchConfig& video, const BranchConfig& audio)
		: m_muxer{ muxer }
		, m_demuxer{ demuxer }
		, m_video{ video }
		, m_audio{ audio }
	{
	}

	void Run()
	{
		std::int64_t videoFrameCounter = 0;
		std::int64_t audioPtsSamples = 0;

		const FrameProcessor videoFrameProcessor = [&videoFrameCounter](Frame& frame) {
			frame->pts = videoFrameCounter++;
		};

		const FrameProcessor audioFrameProcessor = [&audioPtsSamples](Frame& frame) {
			frame->pts = audioPtsSamples;
			audioPtsSamples += frame->nb_samples;
		};

		while (true)
		{
			auto demuxResult = m_demuxer.TryRead();
			if (!demuxResult)
			{
				throw Exception(demuxResult.error());
			}
			auto& pkt = *demuxResult;

			if (!pkt)
			{
				break;
			}

			const int streamIndex = pkt->stream_index;

			if (streamIndex == m_video.streamIndex)
			{
				SendPacket(std::move(pkt), m_video, videoFrameProcessor);

				// Drain decoder after sending
				DrainDecoder(m_video, videoFrameProcessor);
			}
			else if (streamIndex == m_audio.streamIndex)
			{
				SendPacket(std::move(pkt), m_audio, audioFrameProcessor);

				// Drain decoder after sending
				DrainDecoder(m_audio, audioFrameProcessor);
			}
		}

		// Flush decoders and encoders
		auto flushPkt = Packet::Null();

		if (auto videoSendResult = m_video.decoder.TrySend(flushPkt))
		{
			DrainDecoder(m_video, videoFrameProcessor);
		}
		if (auto audioSendResult = m_audio.decoder.TrySend(flushPkt))
		{
			DrainDecoder(m_audio, audioFrameProcessor);
		}

		// Flush encoders
		FlushEncoder(m_video);
		FlushEncoder(m_audio);
	}

private:
	struct Branch
	{
		explicit Branch(const BranchConfig& cfg)
			: decoder{ cfg.decoder }
			, encoder{ cfg.encoder }
			, streamIndex{ cfg.streamIndex }
			, track{ cfg.track }
		{
		}
		Decoder& decoder;
		Encoder& encoder;
		int streamIndex = -1;
		Muxer::TrackId track;
	};

	using FrameProcessor = std::function<void(Frame&)>;

	void SendPacket(Packet&& packet, Branch& branch, const FrameProcessor& frameProcessor)
	{
		while (true)
		{
			auto sendResult = branch.decoder.TrySend(packet);
			if (!sendResult)
			{
				throw Exception(sendResult.error());
			}

			if (*sendResult == ffmpeg::SendResult::Accepted)
				break;

			DrainDecoder(branch, frameProcessor);
		}
	}

	void DrainDecoder(Branch& branch, const FrameProcessor& frameProcessor)
	{
		Frame frame;
		while (true)
		{
			auto recvResult = branch.decoder.TryReceive(frame);
			if (!recvResult)
			{
				throw Exception(recvResult.error());
			}
			if (*recvResult == ffmpeg::ReceiveResult::EndOfStream)
				break;
			if (*recvResult == ffmpeg::ReceiveResult::NeedSend)
				break;
			frameProcessor(frame);

			SendFrameToEncoder(frame, branch);

			DrainEncoder(branch);
		}
	}

	// Returns true if stream needs send and false on End of Stream
	bool DrainEncoder(Branch& branch)
	{
		using ffmpeg::ReceiveResult;

		while (true)
		{
			Packet pkt;
			auto recvResult = branch.encoder.TryReceive(pkt);
			if (!recvResult)
			{
				throw Exception(recvResult.error());
			}
			switch (*recvResult)
			{
			case ReceiveResult::Produced:
				m_muxer.WritePacket(branch.track, *pkt, branch.encoder.GetStreamTimeBase().ToAV());
				break;
			case ReceiveResult::NeedSend:
				return true;
			case ReceiveResult::EndOfStream:
				return false;
			}
		}
	}

	void SendFrameToEncoder(Frame& frame, Branch& branch)
	{
		// Send frame to encoder
		while (true)
		{
			auto sendResult = branch.encoder.TrySend(frame);
			if (!sendResult)
			{
				throw Exception(sendResult.error());
			}
			if (*sendResult == ffmpeg::SendResult::Accepted || *sendResult == ffmpeg::SendResult::Flushed)
				break;

			if (!DrainEncoder(branch))
			{
				break;
			}
		}
	}

	void FlushEncoder(Branch& branch)
	{
		auto sendResult = branch.encoder.TrySend(Frame::Null());
		if (!sendResult)
		{
			throw Exception(sendResult.error());
		}

		DrainEncoder(branch);
	}

	Muxer& m_muxer;
	Demuxer& m_demuxer;
	Branch m_audio;
	Branch m_video;
};

} // namespace mm_pipeline