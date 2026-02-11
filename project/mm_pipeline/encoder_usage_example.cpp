// Example usage of mm_pipeline::Encoder
//
// This file demonstrates how to use the Encoder class for encoding video/audio frames.

import std;
import mm_pipeline.encoder;
import mm_pipeline.error;
import ffmpeg.frame;
import ffmpeg.packet;
import ffmpeg.rational;
import ffmpeg.codec;

extern "C" {
#include <libavcodec/avcodec.h>
}

void EncoderUsageExample()
{
	using mm_pipeline::Encoder;
	using ffmpeg::Frame;
	using ffmpeg::Packet;
	using ffmpeg::Rational;

	// Step 1: Find the codec you want to use (e.g., H.264 for video)
	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec)
	{
		std::cerr << "Codec not found\n";
		return;
	}

	// Step 2: Create the encoder with the codec and stream time base
	// Time base is typically 1/framerate or similar
	Rational timeBase{ 1, 30 }; // 30 fps
	Encoder encoder{ codec, timeBase };

	// Step 3: Configure the encoder context (REQUIRED before TryOpen)
	// Access the context and set encoding parameters
	auto& ctx = encoder.Context();
	ctx->width = 1920;
	ctx->height = 1080;
	ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	ctx->bit_rate = 4000000; // 4 Mbps
	ctx->gop_size = 12; // GOP size
	ctx->max_b_frames = 2;

	// Step 4: Open the encoder
	auto openResult = encoder.TryOpen(/* options */ nullptr);
	if (!openResult)
	{
		std::cerr << "Failed to open encoder: " << openResult.error().where << "\n";
		return;
	}

	// Step 5: Send frames for encoding
	Frame frame; // Assume this is filled with actual frame data
	// ... (fill frame with pixel data)

	auto sendResult = encoder.TrySend(frame);
	if (!sendResult)
	{
		std::cerr << "Failed to send frame: " << sendResult.error().where << "\n";
		return;
	}

	switch (*sendResult)
	{
	case ffmpeg::SendResult::Accepted:
		std::cout << "Frame accepted by encoder\n";
		break;
	case ffmpeg::SendResult::NeedReceive:
		std::cout << "Encoder needs to output packets before accepting more frames\n";
		break;
	case ffmpeg::SendResult::Flushed:
		std::cout << "Encoder is in flush mode\n";
		break;
	}

	// Step 6: Receive encoded packets
	Packet pkt;
	auto receiveResult = encoder.TryReceive(pkt);
	if (!receiveResult)
	{
		std::cerr << "Failed to receive packet: " << receiveResult.error().where << "\n";
		return;
	}

	switch (*receiveResult)
	{
	case ffmpeg::ReceiveResult::Produced:
		std::cout << "Packet produced! Size: " << pkt->size << " bytes\n";
		// Write pkt to muxer or file
		break;
	case ffmpeg::ReceiveResult::NeedSend:
		std::cout << "Encoder needs more input frames\n";
		break;
	case ffmpeg::ReceiveResult::EndOfStream:
		std::cout << "Encoder finished (after flush)\n";
		break;
	}

	// Step 7: Flush the encoder (send empty frame)
	// This is typically done at the end when no more frames to encode
	Frame emptyFrame{ nullptr }; // Empty frame signals flush
	auto flushSend = encoder.TrySend(emptyFrame);
	if (flushSend && *flushSend == ffmpeg::SendResult::Flushed)
	{
		std::cout << "Encoder flushed, draining packets...\n";

		// Keep receiving until EndOfStream
		while (true)
		{
			Packet drainPkt;
			auto drainResult = encoder.TryReceive(drainPkt);
			if (!drainResult)
			{
				std::cerr << "Error during drain: " << drainResult.error().where << "\n";
				break;
			}

			if (*drainResult == ffmpeg::ReceiveResult::EndOfStream)
			{
				std::cout << "All packets drained\n";
				break;
			}

			if (*drainResult == ffmpeg::ReceiveResult::Produced)
			{
				std::cout << "Drained packet: " << drainPkt->size << " bytes\n";
			}
		}
	}

	// Step 8: Export codec parameters (for muxer)
	AVCodecParameters* codecpar = avcodec_parameters_alloc();
	auto paramsResult = encoder.TryToCodecParameters(*codecpar);
	if (!paramsResult)
	{
		std::cerr << "Failed to export parameters: " << paramsResult.error().where << "\n";
	}
	else
	{
		std::cout << "Codec parameters exported successfully\n";
		// Pass codecpar to muxer.AddTrack() etc.
	}
	avcodec_parameters_free(&codecpar);
}

// Typical encoding loop pattern
void TypicalEncodingLoop()
{
	using mm_pipeline::Encoder;
	using ffmpeg::Frame;
	using ffmpeg::Packet;

	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	Encoder encoder{ codec, {1, 30} };

	// Configure
	encoder.Context()->width = 1280;
	encoder.Context()->height = 720;
	encoder.Context()->pix_fmt = AV_PIX_FMT_YUV420P;

	// Open
	if (auto r = encoder.TryOpen(); !r)
	{
		return;
	}

	// Encode frames
	std::vector<Frame> framesToEncode; // Assume filled with frames

	for (auto& frame : framesToEncode)
	{
		// Send frame
		while (true)
		{
			auto sendResult = encoder.TrySend(frame);
			if (!sendResult)
				return;

			if (*sendResult == ffmpeg::SendResult::Accepted)
				break;

			if (*sendResult == ffmpeg::SendResult::NeedReceive)
			{
				// Drain packets
				Packet pkt;
				auto recvResult = encoder.TryReceive(pkt);
				if (recvResult && *recvResult == ffmpeg::ReceiveResult::Produced)
				{
					// Write pkt somewhere
				}
			}
		}

		// Try to receive packets
		while (true)
		{
			Packet pkt;
			auto recvResult = encoder.TryReceive(pkt);
			if (!recvResult)
				return;

			if (*recvResult == ffmpeg::ReceiveResult::NeedSend)
				break;

			if (*recvResult == ffmpeg::ReceiveResult::Produced)
			{
				// Write pkt somewhere
			}
		}
	}

	// Flush
	Frame emptyFrame{ nullptr };
	encoder.TrySend(emptyFrame);

	// Drain
	while (true)
	{
		Packet pkt;
		auto recvResult = encoder.TryReceive(pkt);
		if (!recvResult || *recvResult == ffmpeg::ReceiveResult::EndOfStream)
			break;
		if (*recvResult == ffmpeg::ReceiveResult::Produced)
		{
			// Write pkt somewhere
		}
	}
}
