export module mm_pipeline.async_encoder2;

import ffmpeg.packet;
import ffmpeg.frame;
import ffmpeg.codec;
import mm_pipeline.async_stage;
import mm_pipeline.encoder;

namespace mm_pipeline
{

export using AsyncEncoder2 = AsyncStage<Encoder, ffmpeg::Frame, ffmpeg::Packet>;

} // namespace mm_pipeline