/*
 * Synchronous Transcoder Demo
 *
 * ASSUMPTIONS AND LIMITATIONS:
 * 1. Input has exactly ONE video stream and ONE audio stream (typical MP4)
 * 2. Video decoder outputs AV_PIX_FMT_YUV420P compatible with x264
 * 3. Audio decoder outputs format compatible with AAC encoder (no resampling)
 * 4. No filtering, scaling, or format conversion (no swscale/swr)
 * 5. Simple PTS scheme:
 *    - Video: frame->pts = frame_index in time_base 1/fps
 *    - Audio: frame->pts increments by nb_samples in time_base 1/sample_rate
 * 6. Minimal encoding options (preset, CRF for x264)
 * 7. Uses av_interleaved_write_frame for proper multi-stream muxing
 *
 * PURPOSE:
 * Validates end-to-end architecture: Demuxer → Decoder → Encoder → Muxer
 * with proper send/receive pump logic respecting SendResult/ReceiveResult.
 */

import std;
import mm_pipeline.demuxer;
import mm_pipeline.decoder;
import mm_pipeline.encoder;
import mm_pipeline.muxer;
import mm_pipeline.error;
import mm_pipeline.async_encoder;
import mm_pipeline.async_decoder;
import mm_pipeline.async_transcoder;
import ffmpeg.dictionary;
import ffmpeg.packet;
import ffmpeg.frame;
import ffmpeg.codec;
import ffmpeg.rational;
import ffmpeg.error;

// Need FFmpeg C API for codec lookup and configuration
#include "../ffmpeg_cpp/src/avcodec.hpp"
#include "../ffmpeg_cpp/src/avformat.hpp"
extern "C" {
#include <libavutil/log.h>
}

// Helper to find stream indices
struct StreamIndices
{
	int video = -1;
	int audio = -1;
};

StreamIndices FindStreams(const mm_pipeline::Demuxer& demuxer)
{
	StreamIndices indices;
	const unsigned int streamCount = demuxer.GetStreamCount();

	for (unsigned int i = 0; i < streamCount; ++i)
	{
		const auto info = demuxer.GetStreamInfo(i);
		if (info.type == AVMEDIA_TYPE_VIDEO && indices.video == -1)
		{
			indices.video = info.index;
		}
		else if (info.type == AVMEDIA_TYPE_AUDIO && indices.audio == -1)
		{
			indices.audio = info.index;
		}
	}

	if (indices.video == -1)
		throw std::runtime_error("No video stream found");
	if (indices.audio == -1)
		throw std::runtime_error("No audio stream found");

	return indices;
}

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <input.mp4> <output.mp4>\n";
		return 1;
	}

	const char* inputPath = argv[1];
	const char* outputPath = argv[2];

	try
	{
		std::cout << "Opening input: " << inputPath << "\n";

		// Step 1: Open demuxer and identify streams
		mm_pipeline::Demuxer demuxer{ inputPath };
		const auto indices = FindStreams(demuxer);

		std::cout << "Found video stream: " << indices.video << "\n";
		std::cout << "Found audio stream: " << indices.audio << "\n";

		const auto videoInfo = demuxer.GetStreamInfo(static_cast<unsigned int>(indices.video));
		const auto audioInfo = demuxer.GetStreamInfo(static_cast<unsigned int>(indices.audio));

		const ffmpeg::Rational videoInTimeBase{ videoInfo.timeBase };
		const ffmpeg::Rational audioInTimeBase{ audioInfo.timeBase };

		// Step 2: Create decoders
		mm_pipeline::Decoder videoDec{ *videoInfo.codecPar, videoInTimeBase };
		mm_pipeline::Decoder audioDec{ *audioInfo.codecPar, audioInTimeBase };

		std::cout << "Created decoders\n";

		// Step 3: Create encoders
		const AVCodec* x264 = avcodec_find_encoder_by_name("libx264");
		if (!x264)
			throw std::runtime_error("libx264 encoder not found");

		const AVCodec* aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!aac)
			throw std::runtime_error("AAC encoder not found");

		// Determine video FPS from input using avgFrameRate
		AVRational fps = videoInfo.avgFrameRate.ToAV();
		if (fps.num <= 0 || fps.den <= 0)
		{
			fps = { 30, 1 }; // Fallback to 30 fps
		}
		// Encoder time_base should be 1/fps (e.g., for 30fps: 1/30)
		const ffmpeg::Rational videoEncTimeBase{ fps.den, fps.num };

		// Audio time base: 1/sample_rate
		const ffmpeg::Rational audioEncTimeBase{ 1, audioInfo.codecPar->sample_rate };

		mm_pipeline::Encoder videoEnc{ x264, videoEncTimeBase };
		mm_pipeline::Encoder audioEnc{ aac, audioEncTimeBase };

		// Configure video encoder
		AVCodecContext* videoCtx = videoEnc.Context().get();
		videoCtx->width = videoInfo.codecPar->width;
		videoCtx->height = videoInfo.codecPar->height;
		videoCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		videoCtx->time_base = videoEncTimeBase.ToAV();
		videoCtx->framerate = fps;
		videoCtx->gop_size = (fps.num / fps.den) * 2; // 2 seconds
		videoCtx->max_b_frames = 2;
		videoCtx->pkt_timebase = videoCtx->time_base; // Ensure packet time_base matches encoder time_base

		// Configure audio encoder
		AVCodecContext* audioCtx = audioEnc.Context().get();
		audioCtx->sample_rate = audioInfo.codecPar->sample_rate;
		audioCtx->ch_layout = audioInfo.codecPar->ch_layout;
		audioCtx->time_base = audioEncTimeBase.ToAV();
		audioCtx->pkt_timebase = audioCtx->time_base; // Ensure packet time_base matches encoder time_base
		audioCtx->bit_rate = 128000; // Set AAC bitrate to 128 kbps

		// Pick first supported sample format for AAC
		// Note: AVCodec::sample_fmts is deprecated; use avcodec_get_supported_config() instead
		const AVSampleFormat* fmts = nullptr;
		int fmtsCount = 0;

		int rc = avcodec_get_supported_config(
			audioCtx,
			aac,
			AV_CODEC_CONFIG_SAMPLE_FORMAT,
			0,
			reinterpret_cast<const void**>(&fmts),
			&fmtsCount);

		if (rc >= 0 && fmts && fmtsCount > 0)
		{
			// Demo: pick first supported format
			audioCtx->sample_fmt = fmts[0];
		}
		else
		{
			// Fallback: use AV_SAMPLE_FMT_FLTP (common for AAC)
			audioCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
		}

		// Open encoders
		ffmpeg::Dictionary videoOpts;
		auto videoSetPreset = videoOpts.TrySet("preset", "medium");
		if (!videoSetPreset)
		{
			std::cerr << "Failed to set preset: " << videoSetPreset.error().code << "\n";
			return 1;
		}
		auto videoSetCrf = videoOpts.TrySet("crf", "23");
		if (!videoSetCrf)
		{
			std::cerr << "Failed to set crf: " << videoSetCrf.error().code << "\n";
			return 1;
		}

		auto videoOpenResult = videoEnc.TryOpen(videoOpts.Ptr());
		if (!videoOpenResult)
		{
			std::cerr << "Failed to open video encoder: " << videoOpenResult.error().code << "\n";
			return 1;
		}

		auto audioOpenResult = audioEnc.TryOpen(nullptr);
		if (!audioOpenResult)
		{
			std::cerr << "Failed to open audio encoder: " << audioOpenResult.error().code << "\n";
			return 1;
		}

		std::cout << "Opened encoders\n";

		// Step 4: Create muxer and add tracks
		mm_pipeline::Muxer muxer{ outputPath, "mp4" };

		AVCodecParameters videoOutPar{};
		auto videoToCodecPar = videoEnc.TryToCodecParameters(videoOutPar);
		if (!videoToCodecPar)
		{
			std::cerr << "Failed to get video codec parameters: " << videoToCodecPar.error().code << "\n";
			return 1;
		}
		auto videoTrack = muxer.AddTrack(videoOutPar, videoEncTimeBase);

		AVCodecParameters audioOutPar{};
		auto audioToCodecPar = audioEnc.TryToCodecParameters(audioOutPar);
		if (!audioToCodecPar)
		{
			std::cerr << "Failed to get audio codec parameters: " << audioToCodecPar.error().code << "\n";
			return 1;
		}
		auto audioTrack = muxer.AddTrack(audioOutPar, audioEncTimeBase);

		muxer.WriteHeader();

		std::cout << "Muxer ready, starting transcode...\n";

		av_log_set_level(AV_LOG_ERROR);

		mm_pipeline::AsyncDecoder asyncAudioDecoder{ std::move(audioDec), 10, 10 };
		mm_pipeline::AsyncEncoder asyncAudioEncoder{ std::move(audioEnc), 10, 10 };
		mm_pipeline::AsyncEncoder asyncVideoEncoder{ std::move(videoEnc), 20, 4 };
		mm_pipeline::AsyncDecoder asyncVideoDecoder{ std::move(videoDec), 4, 20 };

		mm_pipeline::AsyncTranscoder asyncTranscoder{
			muxer, demuxer,
			{ asyncVideoDecoder, asyncVideoEncoder, indices.video, videoTrack },
			{ asyncAudioDecoder, asyncAudioEncoder, indices.audio, audioTrack }
		};

		using namespace std::chrono;
		using Clock = high_resolution_clock;

		auto start = Clock::now();

		asyncTranscoder.Run();

		auto end = Clock::now();

		auto printStats = [](const auto& stats, std::string_view name) {
			std::cout << name
					  << " stats:\n num locks: " << stats.numLockAcquisitions
					  << "\n wait duration: " << duration<double>(stats.waitDuration)
					  << "\n max size: " << stats.maxQueueSize
					  << "\n avg size: " << static_cast<double>(stats.numItems) / stats.numUpdates
					  << "\n";
		};

		printStats(asyncVideoDecoder.GetInputQueueStats(), "Video decoder input queue");
		printStats(asyncVideoDecoder.GetOutputQueueStats(), "Video decoder output queue");
		printStats(asyncVideoEncoder.GetInputQueueStats(), "Video encoder input queue");
		printStats(asyncVideoEncoder.GetOutputQueueStats(), "Video encoder output queue");

		printStats(asyncAudioDecoder.GetInputQueueStats(), "Audio decoder input queue");
		printStats(asyncAudioDecoder.GetOutputQueueStats(), "Audio decoder output queue");
		printStats(asyncAudioEncoder.GetInputQueueStats(), "Audio encoder input queue");
		printStats(asyncAudioEncoder.GetOutputQueueStats(), "Audio encoder output queue");

		std::cout << "Finished in " << duration<double>(end - start).count() << " s.\n";

		// Step 6: Close muxer
		muxer.Close();

		std::cout << "Output written to: " << outputPath << "\n";
		std::cout << "Packets written: " << asyncTranscoder.GetPacketsWritten() << "\n";
		return 0;
	}
	catch (const mm_pipeline::Exception& e)
	{
		std::cerr << "mm_pipeline error: " << e.what() << " (code=" << e.GetError().code << ")\n";
		return 1;
	}
	catch (const ffmpeg::Exception& e)
	{
		std::cerr << "FFmpeg error: " << e.what() << " (code=" << e.GetError().code << ")\n";
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
