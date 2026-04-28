export module mm_pipeline.async_decoder2;

import ffmpeg.packet;
import ffmpeg.frame;
import ffmpeg.codec;
import mm_pipeline.async_stage;
import mm_pipeline.decoder;

namespace mm_pipeline
{

export using AsyncDecoder2 = AsyncStage<Decoder, ffmpeg::Packet, ffmpeg::Frame>;

} // namespace mm_pipeline