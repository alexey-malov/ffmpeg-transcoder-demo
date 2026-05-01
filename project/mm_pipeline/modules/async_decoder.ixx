export module mm_pipeline.async_decoder;

import ffmpeg.packet;
import ffmpeg.frame;
import mm_pipeline.async_stage;
import mm_pipeline.decoder;

namespace mm_pipeline
{

export using AsyncDecoder = AsyncStage<Decoder, ffmpeg::Packet, ffmpeg::Frame>;

} // namespace mm_pipeline