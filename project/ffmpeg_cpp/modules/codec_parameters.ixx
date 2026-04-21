module;

#include "../src/codec_par.hpp"

export module ffmpeg.codec_parameters;
import ffmpeg.error;
import std;

namespace ffmpeg
{

class CodecParameters
{
public:
	CodecParameters()
		: m_params{ avcodec_parameters_alloc() }
	{
		if (!m_params)
		{
			ThrowFFmpegNoMem("avcodec_parameters_alloc");
		}
	}

	CodecParameters(const AVCodecParameters& params)
		: CodecParameters{}
	{
		Check(TryCopyFrom(params));
	}

	CodecParameters(const CodecParameters& other)
		: CodecParameters{ *other.m_params }
	{
	}

	CodecParameters(CodecParameters&&) noexcept = default;

	[[nodiscard]] std::expected<void, Error> TryCopyFrom(const AVCodecParameters& params) noexcept
	{
		return ExpectedFromErrorCode(avcodec_parameters_copy(m_params.get(), &params), "avcodec_parameters_copy");
	}

	CodecParameters& operator=(const AVCodecParameters& params)
	{
		if (m_params.get() != &params)
		{
			Check(TryCopyFrom(params));
		}
		return *this;
	}
	CodecParameters& operator=(const CodecParameters& other)
	{
		return *this = *other.m_params;
	}

	CodecParameters& operator=(CodecParameters&&) noexcept = default;

	template <typename Self>
	[[nodiscard]] auto get(this Self& self) noexcept
		-> std::conditional_t<std::is_const_v<Self>, const AVCodecParameters*, AVCodecParameters*>
	{
		return self.m_params.get();
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
		void operator()(AVCodecParameters* params) noexcept
		{
			avcodec_parameters_free(&params);
		}
	};
	std::unique_ptr <AVCodecParameters, Deleter> m_params;
};

} // namespace ffmpeg
