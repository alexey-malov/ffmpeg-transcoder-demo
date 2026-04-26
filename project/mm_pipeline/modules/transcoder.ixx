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
		m_packetsWritten = 0;
		std::int64_t videoFrameCounter = 0;
		std::int64_t audioPtsSamples = 0;

		const FrameProcessor videoFrameProcessor = [&videoFrameCounter](Frame& frame) {
			frame->pts = videoFrameCounter++;
		};

		const FrameProcessor audioFrameProcessor = [&audioPtsSamples](Frame& frame) {
			frame->pts = audioPtsSamples;
			audioPtsSamples += frame->nb_samples;
		};

		for (Packet pkt; pkt = m_demuxer.Read();)
		{
			if (pkt->stream_index == m_video.streamIndex)
			{
				SendPacket(pkt, m_video, videoFrameProcessor);
			}
			else if (pkt->stream_index == m_audio.streamIndex)
			{
				SendPacket(pkt, m_audio, audioFrameProcessor);
			}
		}

		Flush(m_video, videoFrameProcessor);
		Flush(m_audio, audioFrameProcessor);
	}

	[[nodiscard]] std::uint64_t GetPacketsWritten() const noexcept { return m_packetsWritten; }

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

	void SendPacket(const Packet& packet, Branch& branch, const FrameProcessor& frameProcessor)
	{
		while (branch.decoder.Send(packet) != ffmpeg::SendResult::Accepted)
		{
			DrainDecoder(branch, frameProcessor);
		}
		DrainDecoder(branch, frameProcessor);
	}

	void DrainDecoder(Branch& branch, const FrameProcessor& frameProcessor)
	{
		Frame frame;
		while (branch.decoder.Receive(frame) == ffmpeg::ReceiveResult::Produced)
		{
			frameProcessor(frame);

			SendFrame(frame, branch);
		}
	}

	// true  -> encoder needs more input
	// false -> encoder reached EOF
	bool DrainEncoder(Branch& branch)
	{
		Packet pkt;
		ffmpeg::ReceiveResult recvResult;
		while ((recvResult = branch.encoder.Receive(pkt)) == ffmpeg::ReceiveResult::Produced)
		{
			m_muxer.WritePacket(branch.track, *pkt, branch.encoder.GetStreamTimeBase().ToAV());
			++m_packetsWritten;
		}
		return recvResult == ffmpeg::ReceiveResult::NeedSend;
	}

	void SendFrame(const Frame& frame, Branch& branch)
	{
		while (branch.encoder.Send(frame) == ffmpeg::SendResult::NeedReceive)
		{
			if (!DrainEncoder(branch))
			{
				break;
			}
		}
		DrainEncoder(branch);
	}

	void Flush(Branch& branch, const FrameProcessor& frameProcessor)
	{
		SendPacket(Packet::Null(), branch, frameProcessor);
		SendFrame(Frame::Null(), branch);
	}

	Muxer& m_muxer;
	Demuxer& m_demuxer;
	Branch m_video;
	Branch m_audio;
	std::uint64_t m_packetsWritten = 0;
};

} // namespace mm_pipeline