export module mm_pipeline.async_transcoder2;

import std;
import mm_pipeline.encoder;
import mm_pipeline.decoder;
import mm_pipeline.async_encoder2;
import mm_pipeline.async_decoder2;
import mm_pipeline.pipeline_notifier;
import mm_pipeline.muxer;
import mm_pipeline.demuxer;
import ffmpeg.packet;
import ffmpeg.frame;

namespace mm_pipeline
{

export class AsyncTranscoder2
{
public:
	using Frame = ffmpeg::Frame;
	using Packet = ffmpeg::Packet;

	struct BranchConfig
	{
		AsyncDecoder2& decoder;
		AsyncEncoder2& encoder;
		int streamIndex = -1;
		Muxer::TrackId track;
	};

	AsyncTranscoder2(Muxer& muxer, Demuxer& demuxer,
		const BranchConfig& video, const BranchConfig& audio,
		mm_pipeline::PipelineNotifier& pipelineNotifier)
		: m_muxer{ muxer }
		, m_demuxer{ demuxer }
		, m_video{ video.decoder, video.encoder, video.streamIndex, video.track }
		, m_audio{ audio.decoder, audio.encoder, audio.streamIndex, audio.track }
		, m_pipelineNotifier{ pipelineNotifier }
	{
	}

	void Run()
	{
		m_video.decoder.Start();
		m_video.encoder.Start();
		m_audio.decoder.Start();
		m_audio.encoder.Start();

		std::int64_t videoFrameCounter = 0;
		std::int64_t audioPtsSamples = 0;
		m_packetsWritten = 0;

		const FrameProcessor videoFrameProcessor = [&videoFrameCounter](Frame& frame) {
			frame->pts = videoFrameCounter++;
			frame->pict_type = AV_PICTURE_TYPE_NONE;
		};

		const FrameProcessor audioFrameProcessor = [&audioPtsSamples](Frame& frame) {
			frame->pts = audioPtsSamples;
			audioPtsSamples += frame->nb_samples;
		};

		try
		{
			while (!AllDone())
			{
				auto epoch = m_pipelineNotifier.epoch.load(std::memory_order_relaxed);

				bool progress = false;

				progress |= DrainEncoder(m_video);
				progress |= DrainEncoder(m_audio);

				progress |= DrainDecoder(m_video, videoFrameProcessor);
				progress |= DrainDecoder(m_audio, audioFrameProcessor);

				progress |= FeedDemux();

				if (!progress)
				{
					m_pipelineNotifier.Wait(epoch);
				}
			}

			m_video.decoder.Join();
			m_video.encoder.Join();
			m_audio.decoder.Join();
			m_audio.encoder.Join();

			m_video.decoder.RethrowIfFailed();
			m_video.encoder.RethrowIfFailed();
			m_audio.decoder.RethrowIfFailed();
			m_audio.encoder.RethrowIfFailed();
		}
		catch (...)
		{
			RequestStop();

			m_video.decoder.Join();
			m_video.encoder.Join();
			m_audio.decoder.Join();
			m_audio.encoder.Join();

			throw;
		}
	}

	void RequestStop()
	{
		m_video.decoder.RequestStop();
		m_video.encoder.RequestStop();
		m_audio.decoder.RequestStop();
		m_audio.encoder.RequestStop();
	}

	[[nodiscard]] std::uint64_t GetPacketsWritten() const noexcept { return m_packetsWritten; }

private:
	using FrameProcessor = std::function<void(Frame& frame)>;

	struct Branch
	{
		AsyncDecoder2& decoder;
		AsyncEncoder2& encoder;
		int streamIndex = -1;
		Muxer::TrackId track;

		bool decoderInputClosed = false;
		bool decoderEof = false;
		bool encoderInputClosed = false;
		bool encoderEof = false;

		std::optional<Packet> pendingPacket;

		std::optional<Frame> pendingFrame;
	};

	bool AllDone() const noexcept
	{
		return m_video.encoderEof && m_audio.encoderEof;
	}

	// Returns true if any progress was made.
	bool DrainEncoder(Branch& branch)
	{
		bool progress = false;

		while (true)
		{
			auto popResult = branch.encoder.TryPop();
			if (!popResult)
			{
				switch (popResult.error())
				{
				case AsyncEncoder2::TryPopError::Empty:
					return progress;

				case AsyncEncoder2::TryPopError::Stopped:
					throw AsyncEncoder2::CancelException{};

				case AsyncEncoder2::TryPopError::Failed:
					branch.encoder.RethrowIfFailed();
					throw std::logic_error("AsyncEncoder::TryPop returned Failed but no exception was stored");
				}
			}

			auto pkt = std::move(*popResult);

			if (!pkt)
			{
				branch.encoderEof = true;
				return true;
			}

			m_muxer.WritePacket(branch.track, *pkt, branch.encoder.GetProcessor().GetStreamTimeBase().ToAV());
			++m_packetsWritten;
			progress = true;
		}
	}

	bool DrainDecoder(Branch& branch, const FrameProcessor& frameProcessor)
	{
		bool progress = false;

		// 1. Сначала пытаемся протолкнуть ранее не принятый encoder-ом frame.
		if (branch.pendingFrame)
		{
			auto pushResult = branch.encoder.TryPush(std::move(*branch.pendingFrame));
			switch (pushResult)
			{
			case AsyncEncoder2::TryPushResult::Ok:
				branch.pendingFrame.reset();
				progress = true;
				break;

			case AsyncEncoder2::TryPushResult::Full:
				return progress;

			case AsyncEncoder2::TryPushResult::Stopped:
				throw AsyncEncoder2::CancelException{};

			case AsyncEncoder2::TryPushResult::Failed:
				branch.encoder.RethrowIfFailed();
				throw std::logic_error("AsyncEncoder::TryPush returned Failed but no exception was stored");
			}
		}

		// 2. Read new frames from decoder
		while (true)
		{
			auto popResult = branch.decoder.TryPop();
			if (!popResult)
			{
				switch (popResult.error())
				{
				case AsyncDecoder2::TryPopError::Empty:
					return progress;

				case AsyncDecoder2::TryPopError::Stopped:
					throw AsyncDecoder2::CancelException{};

				case AsyncDecoder2::TryPopError::Failed:
					branch.decoder.RethrowIfFailed();
					throw std::logic_error("AsyncDecoder::TryPop returned Failed but no exception was stored");
				}
			}

			Frame frame = std::move(*popResult);

			if (!frame)
			{
				branch.decoderEof = true;

				if (!branch.encoderInputClosed)
				{
					branch.encoder.CloseInput();
					branch.encoderInputClosed = true;
				}

				return true;
			}

			frameProcessor(frame);

			auto pushResult = branch.encoder.TryPush(std::move(frame));
			switch (pushResult)
			{
			case AsyncEncoder2::TryPushResult::Ok:
				progress = true;
				break;

			case AsyncEncoder2::TryPushResult::Full:
				branch.pendingFrame = std::move(frame);
				return true;

			case AsyncEncoder2::TryPushResult::Stopped:
				throw AsyncEncoder2::CancelException{};

			case AsyncEncoder2::TryPushResult::Failed:
				branch.encoder.RethrowIfFailed();
				throw std::logic_error("AsyncEncoder::TryPush returned Failed but no exception was stored");
			}
		}
	}

	static std::optional<bool> TryFlushPendingPacket(Branch& branch)
	{
		if (branch.pendingPacket)
		{
			auto pushResult = branch.decoder.TryPush(std::move(*branch.pendingPacket));
			switch (pushResult)
			{
			case AsyncDecoder2::TryPushResult::Ok:
				branch.pendingPacket.reset();
				return true;

			case AsyncDecoder2::TryPushResult::Full:
				return false;

			case AsyncDecoder2::TryPushResult::Stopped:
				throw AsyncDecoder2::CancelException{};

			case AsyncDecoder2::TryPushResult::Failed:
				branch.decoder.RethrowIfFailed();
				throw std::logic_error("AsyncDecoder::TryPush returned Failed but no exception was stored");
			}
		}
		return std::nullopt;
	}

	bool FeedDemux()
	{
		if (m_demuxEof)
		{
			return false;
		}

		// Сначала пытаемся протолкнуть уже прочитанный, но не принятый decoder-ом packet.
		if (auto flushResult = TryFlushPendingPacket(m_video))
		{
			return *flushResult;
		}
		if (auto flushResult = TryFlushPendingPacket(m_audio))
		{
			return *flushResult;
		}

		auto readResult = m_demuxer.TryRead();
		if (!readResult)
		{
			throw Exception{ readResult.error() };
		}

		auto& packet = *readResult;

		if (!packet)
		{
			m_demuxEof = true;
			CloseDecoders();
			return true;
		}

		const int streamIndex = packet->stream_index;

		Branch* branch = nullptr;
		if (streamIndex == m_video.streamIndex)
		{
			branch = &m_video;
		}
		else if (streamIndex == m_audio.streamIndex)
		{
			branch = &m_audio;
		}
		else
		{
			return true;
		}

		auto pushResult = branch->decoder.TryPush(std::move(packet));
		switch (pushResult)
		{
		case AsyncDecoder2::TryPushResult::Ok:
			return true;

		case AsyncDecoder2::TryPushResult::Full:
			branch->pendingPacket = std::move(packet);
			return true;

		case AsyncDecoder2::TryPushResult::Stopped:
			throw AsyncDecoder2::CancelException{};

		case AsyncDecoder2::TryPushResult::Failed:
			branch->decoder.RethrowIfFailed();
			throw std::logic_error("AsyncDecoder::TryPush returned Failed but no exception was stored");
		}

		throw std::logic_error("AsyncDecoder::TryPush returned invalid result");
	}

	void CloseDecoders()
	{
		if (!m_video.decoderInputClosed)
		{
			m_video.decoder.CloseInput();
			m_video.decoderInputClosed = true;
		}
		if (!m_audio.decoderInputClosed)
		{
			m_audio.decoder.CloseInput();
			m_audio.decoderInputClosed = true;
		}
	}

	Muxer& m_muxer;
	Demuxer& m_demuxer;

	Branch m_video;
	Branch m_audio;

	mm_pipeline::PipelineNotifier& m_pipelineNotifier;

	bool m_demuxEof = false;

	std::uint64_t m_packetsWritten = 0;
};

} // namespace mm_pipeline
