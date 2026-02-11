module;

#include "../src/avformat.hpp"
#include <cassert>

export module ffmpeg.input_format_context;

import std;
import ffmpeg.packet;
import ffmpeg.error;
import ffmpeg.unique_handle;

namespace ffmpeg
{

export class InputFormatContext final
{
public:
	InputFormatContext(const char* url,
		const AVInputFormat* inputFormat, AVDictionary** dictionary);

	[[nodiscard]] std::expected<void, Error> TryReadFrame(AVPacket& packet) noexcept;

	[[nodiscard]] std::expected<void, Error> TryFindStreamInfo(AVDictionary** options = nullptr) noexcept;

	void FindStreamInfo(AVDictionary** options = nullptr)
	{
		Check(TryFindStreamInfo(options));
	}

	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
	{
		return self.m_ctx.get();
	}

	template <typename Self>
	[[nodiscard]] decltype(auto) operator*(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return *self.get();
	}

	template <typename Self>
	[[nodiscard]] [[nodiscard]] auto operator->(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return self.get();
	}

private:
	struct Deleter
	{
		void operator()(AVFormatContext* ctx) noexcept;
	};
	using FormatContextPtr = UniqueHandle<AVFormatContext, Deleter>;
	FormatContextPtr m_ctx;
};

} // namespace ffmpeg