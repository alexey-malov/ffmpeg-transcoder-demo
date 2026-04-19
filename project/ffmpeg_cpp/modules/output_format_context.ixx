module;

#include "../src/avformat.hpp"
#include <cassert>

export module ffmpeg.output_format_context;

import std;
import ffmpeg.packet;
import ffmpeg.error;

namespace ffmpeg
{

export class OutputFormatContext final
{
public:
	OutputFormatContext(const char* url, const char* fmtName);

	OutputFormatContext(OutputFormatContext&& other) noexcept;
	OutputFormatContext& operator=(OutputFormatContext&& other) noexcept;

	~OutputFormatContext();

	[[nodiscard]] std::expected<void, Error> TryOpenFileIO() noexcept;
	void OpenFileIO()
	{
		Check(TryOpenFileIO());
	}

	[[nodiscard]] std::expected<void, Error> TryWriteHeader(AVDictionary** options) noexcept;
	void WriteHeader(AVDictionary** options)
	{
		Check(TryWriteHeader(options));
	}

	[[nodiscard]] std::expected<void, Error> TryWritePacket(AVPacket& packet) noexcept;
	void WritePacket(AVPacket& packet)
	{
		Check(TryWritePacket(packet));
	}

	[[nodiscard]] std::expected<void, Error> TryInterleavedWritePacket(AVPacket& packet) noexcept;
	void InterleavedWritePacket(AVPacket& packet)
	{
		Check(TryInterleavedWritePacket(packet));
	}

	[[nodiscard]] std::expected<void, Error> TryWriteTrailer() noexcept;
	void WriteTrailer()
	{
		Check(TryWriteTrailer());
	}

	[[nodiscard]] std::expected<void, Error> TryClose() noexcept;
	void Close()
	{
		Check(TryClose());
	}

	[[nodiscard]] std::expected<AVStream*, Error> TryAddStream() noexcept;

	AVStream* AddStream()
	{
		return Check(TryAddStream());
	}

	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
	{
		return self.m_ctx.get();
	}

	template <typename Self>
	[[nodiscard]] decltype(auto) operator*(this Self& self) noexcept
	{
		return *self.get();
	}

	template <typename Self>
	[[nodiscard]] [[nodiscard]] auto operator->(this Self& self) noexcept
	{
		return self.get();
	}

private:
	struct Deleter
	{
		void operator()(AVFormatContext* ctx) noexcept;
	};
	using FormatContextPtr = std::unique_ptr<AVFormatContext, Deleter>;

	struct State
	{
		bool ioOpened = false;
		bool headerWritten = false;
		bool closed = false;
	};

	[[nodiscard]] Error EnsureWritable(const char* where) const noexcept;

	FormatContextPtr m_ctx;
	State m_state;
};

} // namespace ffmpeg