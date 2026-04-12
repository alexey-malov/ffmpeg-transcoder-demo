module;

#include "../src/packet.hpp"
#include <cassert>

export module ffmpeg.packet;
import std;
import ffmpeg.unique_handle;

namespace ffmpeg
{

export class Packet
{
public:
	Packet();

	static Packet Null() noexcept { return Packet{ nullptr }; }

	Packet(const Packet&) = delete;
	Packet& operator=(const Packet&) = delete;

	Packet(Packet&& other) noexcept = default;

	Packet& operator=(Packet&& other) noexcept = default;

	[[nodiscard]] operator bool() const noexcept
	{
		return m_packet.get() != nullptr;
	}

	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
		-> std::conditional_t<std::is_const_v<Self>, const AVPacket*, AVPacket*>
	{
		return self.m_packet.get();
	}

	template <typename Self>
	[[nodiscard]] decltype(auto) operator*(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return *self.get();
	}

	template <typename Self>
	[[nodiscard]] auto operator->(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return self.get();
	}

private:
	explicit Packet(std::nullptr_t) noexcept
		: m_packet{ nullptr }
	{
	}

	struct Deleter
	{
		void operator()(AVPacket* packet) const noexcept
		{
			av_packet_free(&packet);
		}
	};
	using PacketPtr = UniqueHandle<AVPacket, Deleter>;
	PacketPtr m_packet;
};

} // namespace ffmpeg
