module;
#include "avformat.hpp"
#include <cassert>

module ffmpeg.output_format_context;
import std;

namespace ffmpeg
{

namespace
{
bool NeedsFileIO(const AVFormatContext* ctx) noexcept
{
	return ctx->oformat && (ctx->oformat->flags & AVFMT_NOFILE) == 0;
}
} // namespace

void OutputFormatContext::Deleter::operator()(AVFormatContext* ctx) noexcept
{
	avformat_free_context(ctx);
}

OutputFormatContext::OutputFormatContext(const char* url, const char* fmtName)
	: m_ctx{ [url, fmtName] {
		AVFormatContext* ctx = nullptr;
		CheckFFmpegError(avformat_alloc_output_context2(&ctx, nullptr, fmtName, url), "avformat_alloc_output_context2");
		return FormatContextPtr{ ctx };
	}() }
{
}

OutputFormatContext::OutputFormatContext(OutputFormatContext&& other) noexcept
	: m_ctx(std::move(other.m_ctx))
	, m_state(std::exchange(other.m_state, {}))
{
}

OutputFormatContext& OutputFormatContext::operator=(OutputFormatContext&& other) noexcept
{
	if (this != &other) [[likely]]
	{
		std::ignore = TryClose();

		m_ctx = std::move(other.m_ctx);
		m_state = std::exchange(other.m_state, {});
	}
	return *this;
}

OutputFormatContext::~OutputFormatContext()
{
	std::ignore = TryClose();
}

std::expected<void, Error> OutputFormatContext::TryOpenFileIO() noexcept
{
	assert(m_ctx); // We must not use a moved-from OutputFormatContext
	if (m_state.closed) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryWriteHeader(closed)"));

	if (m_state.ioOpened)
		return {};

	AVFormatContext* ctx = m_ctx.get();
	if (!ctx)
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryOpenFileIO(null ctx)"));

	if (!NeedsFileIO(ctx))
	{
		m_state.ioOpened = true;
		return {};
	}

	if (!ctx->url)
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryOpenFileIO(null url)"));

	if (const int ret = avio_open2(&ctx->pb, ctx->url, AVIO_FLAG_WRITE, nullptr, nullptr); ret < 0) [[unlikely]]
	{
		return std::unexpected(MakeFFmpegError(ret, "avio_open2"));
	}

	auto result = ExpectedFromFFmpegErrorCode(
		avio_open2(&ctx->pb, ctx->url, AVIO_FLAG_WRITE, nullptr, nullptr), "avio_open2");
	m_state.ioOpened = result.has_value();

	return result;
}

std::expected<void, Error> OutputFormatContext::TryWriteHeader(AVDictionary** options) noexcept
{
	assert(m_ctx); // We must not use a moved-from OutputFormatContext
	if (m_state.closed) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryWriteHeader(closed)"));

	if (m_state.headerWritten)
		return {};

	if (auto e = TryOpenFileIO(); !e)
		return std::unexpected(e.error());

	auto result = ExpectedFromFFmpegErrorCode(avformat_write_header(m_ctx.get(), options), "avformat_write_header");

	m_state.headerWritten = result.has_value();

	return {};
}

std::expected<void, Error> OutputFormatContext::TryWritePacket(AVPacket& packet) noexcept
{
	assert(m_ctx); // We must not use a moved-from OutputFormatContext
	if (m_state.closed) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryWritePacket(closed)"));

	if (!m_state.headerWritten)
	{
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "TryWritePacket called before header"));
	}

	return ExpectedFromFFmpegErrorCode(
		av_interleaved_write_frame(m_ctx.get(), &packet), "av_interleaved_write_frame");
}

std::expected<void, Error> OutputFormatContext::TryInterleavedWritePacket(AVPacket& packet) noexcept
{
	assert(m_ctx); // We must not use a moved-from OutputFormatContext
	if (m_state.closed) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryInterleavedWritePacket(closed)"));

	if (!m_state.headerWritten)
	{
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "TryInterleavedWritePacket called before header"));
	}

	return ExpectedFromFFmpegErrorCode(av_interleaved_write_frame(m_ctx.get(), &packet),
		"av_interleaved_write_frame");
}

std::expected<void, Error> OutputFormatContext::TryWriteTrailer() noexcept
{
	assert(m_ctx); // We must not use a moved-from OutputFormatContext
	if (m_state.closed) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "OutputFormatContext::TryWriteTrailer(closed)"));

	if (!m_state.headerWritten)
		return {};

	auto result = ExpectedFromFFmpegErrorCode(av_write_trailer(m_ctx.get()),
		"av_write_trailer");

	m_state.headerWritten = !result.has_value();
	m_state.headerWritten = !result.has_value();

	return result;
}

std::expected<void, Error> OutputFormatContext::TryClose() noexcept
{
	AVFormatContext* const ctx = m_ctx.get();
	assert(ctx); // We must not use a moved-from OutputFormatContext

	if (m_state.closed)
		return {};

	std::expected<void, Error> result = TryWriteTrailer();

	if (ctx->pb && NeedsFileIO(ctx))
	{
		if (const int ret = avio_closep(&ctx->pb); ret < 0 && result) [[unlikely]]
		{
			result = std::unexpected(MakeFFmpegError(ret, "avio_closep"));
		}
	}
	m_state.closed = true;

	return result;
}

Error OutputFormatContext::EnsureWritable(const char* where) const noexcept
{
	if (m_state.closed) [[unlikely]]
		return MakeFFmpegError(AVERROR(EINVAL), where);

	if (!m_ctx.get()) [[unlikely]]
		return MakeFFmpegError(AVERROR(EINVAL), where);

	return {};
}

std::expected<AVStream*, Error> OutputFormatContext::TryAddStream() noexcept
{
	if (auto e = EnsureWritable("OutputFormatContext::TryAddStream(closed ctx)"); e) [[unlikely]]
		return std::unexpected(e);

	if (m_state.headerWritten) [[unlikely]]
		return std::unexpected(MakeFFmpegError(AVERROR(EINVAL), "TryAddStream called after header"));

	AVStream* st = avformat_new_stream(m_ctx.get(), /* codec= */ nullptr);
	if (!st) [[unlikely]]
	{
		return std::unexpected(MakeFFmpegError(AVERROR(ENOMEM), "avformat_new_stream"));
	}

	return st;
}

} // namespace ffmpeg
