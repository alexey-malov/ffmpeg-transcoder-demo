export module mm_pipeline.async_encoder;

import ffmpeg.packet;
import ffmpeg.frame;
import mm_pipeline.async_stage;
import mm_pipeline.encoder;

namespace mm_pipeline
{

export using AsyncEncoder = AsyncStage<Encoder, ffmpeg::Frame, ffmpeg::Packet>;

} // namespace mm_pipeline