module;

#include "../src/frame.hpp"
#include <cassert>

export module ffmpeg.frame;
import std;
import ffmpeg.unique_handle;
import ffmpeg.error;

namespace ffmpeg
{

export class Frame
{
public:
	Frame();

	explicit Frame(std::nullptr_t) noexcept
		: m_frame{ nullptr }
	{
	}

	explicit Frame(AVFrame* frame) noexcept
		: m_frame{ frame }
	{
	}

	[[nodiscard]] Frame Clone() const
	{
		assert(m_frame.get() != nullptr);
		AVFrame* cloned = av_frame_clone(m_frame.get());
		if (!cloned) [[unlikely]]
		{
			ThrowFFmpegNoMem("av_frame_clone");
		}
		return Frame{ cloned };
	}

	[[nodiscard]] explicit operator bool() const noexcept { return m_frame.get() != nullptr; }

	void Unref() noexcept
	{
		if (m_frame.get() != nullptr)
		{
			av_frame_unref(m_frame.get());
		}
	}

	[[nodiscard]] std::expected<void, Error> TryMakeWritable() noexcept
	{
		assert(m_frame.get() != nullptr);
		const int rc = av_frame_make_writable(m_frame.get());
		if (rc < 0) [[unlikely]]
		{
			return std::unexpected(ErrorFromFFmpegErrorCode(rc, "av_frame_make_writable"));
		}
		return {};
	}


	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
		-> std::conditional_t<std::is_const_v<Self>, const AVFrame*, AVFrame*>
	{
		return self.m_frame.get();
	}

	template <typename Self>
	[[nodiscard]] auto operator->(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return self.get();
	}

	template <typename Self>
	[[nodiscard]] decltype(auto) operator*(this Self& self) noexcept
	{
		assert(self.get() != nullptr);
		return *self.get();
	}

private:
	struct Deleter
	{
		void operator()(AVFrame* frame) const noexcept
		{
			av_frame_free(&frame);
		}
	};
	using FramePtr = UniqueHandle<AVFrame, Deleter>;

	FramePtr m_frame;
};

} // namespace ffmpeg
