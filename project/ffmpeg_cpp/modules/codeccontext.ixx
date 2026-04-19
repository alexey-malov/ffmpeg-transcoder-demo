module;
#include "../src/avcodec.hpp"
#include <cassert>

export module ffmpeg.codec;
import ffmpeg.error;
import std;

namespace ffmpeg
{

export enum class SendResult : uint8_t {
	Accepted,
	NeedReceive,
	Flushed,
};

export enum class ReceiveResult : uint8_t {
	Produced,
	NeedSend,
	EndOfStream
};

export class CodecContext
{
public:
	explicit CodecContext(const AVCodec* codec);

	[[nodiscard]] std::expected<void, Error> TryOpen(const AVCodec* codec, AVDictionary** options) noexcept;

	[[nodiscard]] std::expected<void, Error> TryFromCodecParameters(const AVCodecParameters& par) noexcept;
	[[nodiscard]] std::expected<void, Error> TryToCodecParameters(AVCodecParameters& par) const noexcept;

	[[nodiscard]] std::expected<SendResult, Error> TrySendPacket(const AVPacket* pkt) noexcept;
	[[nodiscard]] std::expected<ReceiveResult, Error> TryReceiveFrame(AVFrame& frame) noexcept;

	[[nodiscard]] std::expected<SendResult, Error> TrySendFrame(const AVFrame* frame) noexcept;
	[[nodiscard]] std::expected<ReceiveResult, Error> TryReceivePacket(AVPacket& pkt) noexcept;

	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
		-> std::conditional_t<std::is_const_v<Self>, const AVCodecContext*, AVCodecContext*>
	{
		return self.m_ctx.get();
	}

	template <typename Self>
	[[nodiscard]] auto operator->(this Self& self) noexcept
	{
		assert(self.get());
		return self.get();
	}

	template <typename Self>
	[[nodiscard]] decltype(auto) operator*(this Self& self) noexcept
	{
		assert(self.get());
		return *self.get();
	}

private:
	struct Deleter
	{
		void operator()(AVCodecContext* ctx) noexcept
		{
			avcodec_free_context(&ctx);
		}
	};

	template <typename Self>
	[[nodiscard]] decltype(auto) ctx(this Self& self) noexcept
	{
		auto c = self.get();
		assert(c != nullptr && "CodecContext used after move");
		return c;
	}

	std::unique_ptr<AVCodecContext, Deleter> m_ctx{ nullptr };
};

} // namespace ffmpeg
