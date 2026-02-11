module;
#include "frame.hpp"
#include "error.hpp"

module ffmpeg.frame;
import ffmpeg.error;

import std;

namespace ffmpeg
{

Frame::Frame()
	: m_frame{ av_frame_alloc() }
{
	if (!m_frame) [[unlikely]]
	{
		ThrowFFmpegNoMem("av_frame_alloc");
	}
}

} // namespace ffmpeg
