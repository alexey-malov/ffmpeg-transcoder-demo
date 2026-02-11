module;
#include "packet.hpp"
#include "error.hpp"
#include <cerrno>

module ffmpeg.packet;
import ffmpeg.error;

import std;

namespace ffmpeg
{

Packet::Packet()
	: m_packet{ av_packet_alloc() }
{
	if (!m_packet) [[unlikely]]
	{
		ThrowFFmpegNoMem("av_packet_alloc");
	}
}

} // namespace ffmpeg
