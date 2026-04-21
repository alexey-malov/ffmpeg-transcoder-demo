module;

#include "dictionary.hpp"
#include "error.hpp"

module ffmpeg.dictionary;
import std;

namespace ffmpeg
{

std::expected<void, Error> Dictionary::TrySet(std::string_view key, std::string_view value, int flags) noexcept
{
	try
	{
		// av_dict_set expects null-terminated strings, so we need to create temporary std::string
		std::string keyStr(key);
		std::string valueStr(value);

		return ExpectedFromErrorCode(av_dict_set(&m_dict, keyStr.c_str(), valueStr.c_str(), flags), "av_dict_set");
	}
	catch (...)
	{
		// Handle allocation failures from std::string construction
		return MakeUnexpected(AVERROR(ENOMEM), "ffmpeg::Dictionary::TrySet");
	}
}

std::expected<void, Error> Dictionary::TrySet(std::string_view key, const char* value, int flags) noexcept
{
	try
	{
		std::string keyStr(key);

		return ExpectedFromErrorCode(av_dict_set(&m_dict, keyStr.c_str(), value, flags), "av_dict_set");
	}
	catch (...)
	{
		// Handle allocation failures from std::string construction
		return MakeUnexpected(AVERROR(ENOMEM), "ffmpeg::Dictionary::TrySet");
	}
}

void Dictionary::Set(std::string_view key, std::string_view value, int flags)
{
	std::string keyStr(key);
	std::string valueStr(value);

	CheckErrorCode(av_dict_set(&m_dict, keyStr.c_str(), valueStr.c_str(), flags), "ffmpeg::Dictionary::Set");
}

void Dictionary::Set(std::string_view key, const char* value, int flags)
{
	std::string keyStr(key);

	CheckErrorCode(av_dict_set(&m_dict, keyStr.c_str(), value, flags), "ffmpeg::Dictionary::Set");
}

} // namespace ffmpeg
