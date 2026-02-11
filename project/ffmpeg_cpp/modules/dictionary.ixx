module;

#include "../src/dictionary.hpp"

export module ffmpeg.dictionary;

import std;
import ffmpeg.error;

namespace ffmpeg
{

export class Dictionary
{
public:
	Dictionary() noexcept = default;

	explicit Dictionary(std::nullptr_t) noexcept
	{
	}

	Dictionary(const Dictionary&) = delete;
	Dictionary& operator=(const Dictionary&) = delete;

	Dictionary(Dictionary&& other) noexcept
		: m_dict(std::exchange(other.m_dict, nullptr))
	{
	}

	Dictionary& operator=(Dictionary&& other) noexcept
	{
		if (this != &other) [[likely]]
		{
			Clear();
			m_dict = std::exchange(other.m_dict, nullptr);
		}
		return *this;
	}

	~Dictionary()
	{
		Clear();
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return m_dict != nullptr;
	}

	// Access to underlying storage for FFmpeg APIs that take AVDictionary**
	[[nodiscard]] AVDictionary** Ptr() noexcept
	{
		return &m_dict;
	}

	[[nodiscard]] AVDictionary* Get() noexcept
	{
		return m_dict;
	}

	[[nodiscard]] const AVDictionary* Get() const noexcept
	{
		return m_dict;
	}

	// Set key/value. If value==nullptr, delete the key (FFmpeg semantics).
	[[nodiscard]] std::expected<void, Error> TrySet(std::string_view key, std::string_view value, int flags = 0) noexcept;

	// Convenience: set from C string value (value may be nullptr).
	[[nodiscard]] std::expected<void, Error> TrySet(std::string_view key, const char* value, int flags = 0) noexcept;

	// Optional convenience: throw-based setter
	void Set(std::string_view key, std::string_view value, int flags = 0);
	void Set(std::string_view key, const char* value, int flags = 0);

	// Clear/free contents
	void Clear() noexcept
	{
		av_dict_free(&m_dict);
	}

private:
	AVDictionary* m_dict = nullptr;
};

} // namespace ffmpeg
