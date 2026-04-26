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

		const FrameProcessor processVideoFrame = [&videoFrameCounter](Frame& frame) {
			frame->pts = videoFrameCounter++;
		};

		const FrameProcessor processAudioFrame = [&audioPtsSamples](Frame& frame) {
			frame->pts = audioPtsSamples;
			audioPtsSamples += frame->nb_samples;
		};

		for (Packet pkt; pkt = m_demuxer.Read();)
		{
			if (pkt->stream_index == m_video.streamIndex)
			{
				SendPacket(pkt, m_video, processVideoFrame);
			}
			else if (pkt->stream_index == m_audio.streamIndex)
			{
				SendPacket(pkt, m_audio, processAudioFrame);
			}
		}

		Flush(m_video, processVideoFrame);
		Flush(m_audio, processAudioFrame);
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

	void SendPacket(const Packet& packet, Branch& branch, const FrameProcessor& processor)
	{
		while (branch.decoder.Send(packet) != ffmpeg::SendResult::Accepted)
		{
			DrainDecoder(branch, processor);
		}
		DrainDecoder(branch, processor);
	}

	void DrainDecoder(Branch& branch, const FrameProcessor& process)
	{
		Frame frame;
		while (branch.decoder.Receive(frame) == ffmpeg::ReceiveResult::Produced)
		{
			process(frame);

			SendFrame(frame, branch);
		}
	}

	void SendFrame(const Frame& frame, Branch& branch)
	{
		while (branch.encoder.Send(frame) == ffmpeg::SendResult::NeedReceive)
		{
			DrainEncoder(branch);
		}
		DrainEncoder(branch);
	}

	void DrainEncoder(Branch& branch)
	{
		Packet pkt;
		while (branch.encoder.Receive(pkt) == ffmpeg::ReceiveResult::Produced)
		{
			m_muxer.WritePacket(branch.track, *pkt, branch.encoder.GetStreamTimeBase().ToAV());
			++m_packetsWritten;
		}
	}

	void Flush(Branch& branch, const FrameProcessor& process)
	{
		SendPacket(Packet::Null(), branch, process);
		SendFrame(Frame::Null(), branch);
	}

	Muxer& m_muxer;
	Demuxer& m_demuxer;
	Branch m_video;
	Branch m_audio;
	std::uint64_t m_packetsWritten = 0;
};

} // namespace mm_pipeline