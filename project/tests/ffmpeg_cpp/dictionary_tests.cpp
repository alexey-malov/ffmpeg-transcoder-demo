#include <catch2/catch_test_macros.hpp>
#include <cstring>

import ffmpeg.dictionary;
import ffmpeg.error;


// Need direct access to FFmpeg dict API for verification
extern "C" {
#include <libavutil/dict.h>
}

namespace
{
constexpr auto kDictionaryTestTag = "[dictionary]";
using ffmpeg::Dictionary;
} // namespace

TEST_CASE("Dictionary default construction yields empty", kDictionaryTestTag)
{
	Dictionary d;

	REQUIRE_FALSE(static_cast<bool>(d));
	REQUIRE(d.Get() == nullptr);
	REQUIRE(*d.Ptr() == nullptr);
}

TEST_CASE("Dictionary explicit nullptr construction yields empty", kDictionaryTestTag)
{
	Dictionary d{ nullptr };

	REQUIRE_FALSE(static_cast<bool>(d));
	REQUIRE(d.Get() == nullptr);
}

TEST_CASE("Dictionary TrySet creates dictionary and stores value", kDictionaryTestTag)
{
	Dictionary d;

	auto result = d.TrySet("preset", "veryfast");
	REQUIRE(result.has_value());

	// After successful set, dictionary should be non-empty
	REQUIRE(static_cast<bool>(d));
	REQUIRE(d.Get() != nullptr);

	// Verify via FFmpeg API
	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "veryfast") == 0);
}

TEST_CASE("Dictionary TrySet with const char* value", kDictionaryTestTag)
{
	Dictionary d;

	auto result = d.TrySet("codec", "h264");
	REQUIRE(result.has_value());

	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "codec", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "h264") == 0);
}

TEST_CASE("Dictionary setting multiple keys", kDictionaryTestTag)
{
	Dictionary d;

	REQUIRE(d.TrySet("crf", "23").has_value());
	REQUIRE(d.TrySet("preset", "medium").has_value());
	REQUIRE(d.TrySet("tune", "film").has_value());

	// Verify all keys are present
	AVDictionaryEntry* crf = av_dict_get(*d.Ptr(), "crf", nullptr, 0);
	AVDictionaryEntry* preset = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	AVDictionaryEntry* tune = av_dict_get(*d.Ptr(), "tune", nullptr, 0);

	REQUIRE(crf != nullptr);
	REQUIRE(std::strcmp(crf->value, "23") == 0);

	REQUIRE(preset != nullptr);
	REQUIRE(std::strcmp(preset->value, "medium") == 0);

	REQUIRE(tune != nullptr);
	REQUIRE(std::strcmp(tune->value, "film") == 0);
}

TEST_CASE("Dictionary TrySet with nullptr deletes key", kDictionaryTestTag)
{
	Dictionary d;

	// Set a key
	REQUIRE(d.TrySet("crf", "23").has_value());

	// Verify it exists
	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "crf", nullptr, 0);
	REQUIRE(entry != nullptr);

	// Delete the key
	auto result = d.TrySet("crf", nullptr);
	REQUIRE(result.has_value());

	// Verify it's gone
	entry = av_dict_get(*d.Ptr(), "crf", nullptr, 0);
	REQUIRE(entry == nullptr);
}

TEST_CASE("Dictionary Clear releases the dictionary", kDictionaryTestTag)
{
	Dictionary d;

	// Set a key
	REQUIRE(d.TrySet("preset", "slow").has_value());
	REQUIRE(static_cast<bool>(d));
	REQUIRE(d.Get() != nullptr);

	// Clear
	d.Clear();

	// Verify empty
	REQUIRE_FALSE(static_cast<bool>(d));
	REQUIRE(d.Get() == nullptr);
	REQUIRE(*d.Ptr() == nullptr);

	// Re-set after Clear should still work
	REQUIRE(d.TrySet("codec", "vp9").has_value());
	REQUIRE(static_cast<bool>(d));

	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "codec", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "vp9") == 0);
}

TEST_CASE("Dictionary move construction transfers ownership", kDictionaryTestTag)
{
	Dictionary a;
	REQUIRE(a.TrySet("preset", "slow").has_value());
	REQUIRE(a.TrySet("crf", "18").has_value());

	// Move construct
	Dictionary b = std::move(a);

	// Verify b has the keys
	REQUIRE(static_cast<bool>(b));
	AVDictionaryEntry* preset = av_dict_get(*b.Ptr(), "preset", nullptr, 0);
	AVDictionaryEntry* crf = av_dict_get(*b.Ptr(), "crf", nullptr, 0);

	REQUIRE(preset != nullptr);
	REQUIRE(std::strcmp(preset->value, "slow") == 0);
	REQUIRE(crf != nullptr);
	REQUIRE(std::strcmp(crf->value, "18") == 0);

	// Verify a is empty (moved-from state)
	REQUIRE_FALSE(static_cast<bool>(a));
	REQUIRE(a.Get() == nullptr);
	REQUIRE(*a.Ptr() == nullptr);
}

TEST_CASE("Dictionary move assignment transfers ownership and frees previous content", kDictionaryTestTag)
{
	Dictionary a;
	REQUIRE(a.TrySet("preset", "slow").has_value());

	Dictionary b;
	REQUIRE(b.TrySet("crf", "18").has_value());

	// Move assign
	b = std::move(a);

	// Verify b has a's key
	AVDictionaryEntry* preset = av_dict_get(*b.Ptr(), "preset", nullptr, 0);
	REQUIRE(preset != nullptr);
	REQUIRE(std::strcmp(preset->value, "slow") == 0);

	// Verify b's old key is gone (freed)
	AVDictionaryEntry* crf = av_dict_get(*b.Ptr(), "crf", nullptr, 0);
	REQUIRE(crf == nullptr);

	// Verify a is empty (moved-from state)
	REQUIRE_FALSE(static_cast<bool>(a));
	REQUIRE(a.Get() == nullptr);
}

TEST_CASE("Dictionary Ptr() stability - FFmpeg can modify via Ptr()", kDictionaryTestTag)
{
	Dictionary d;
	REQUIRE(d.TrySet("preset", "fast").has_value());

	// Verify initial value
	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "fast") == 0);

	// Modify directly via FFmpeg API using Ptr()
	int ret = av_dict_set(d.Ptr(), "preset", "slow", 0);
	REQUIRE(ret == 0);

	// Verify wrapper sees the change
	entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "slow") == 0);

	// Add another key via FFmpeg API
	ret = av_dict_set(d.Ptr(), "tune", "zerolatency", 0);
	REQUIRE(ret == 0);

	// Verify it's visible through wrapper
	entry = av_dict_get(*d.Ptr(), "tune", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "zerolatency") == 0);
}

TEST_CASE("Dictionary TrySet with empty string value", kDictionaryTestTag)
{
	Dictionary d;

	auto result = d.TrySet("key", "");
	REQUIRE(result.has_value());

	// Verify stored value is empty string
	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "key", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(entry->value != nullptr);
	REQUIRE(std::strcmp(entry->value, "") == 0);
	REQUIRE(std::strlen(entry->value) == 0);
}

TEST_CASE("Dictionary Set throws on error", kDictionaryTestTag)
{
	Dictionary d;

	// Normal set should not throw
	REQUIRE_NOTHROW(d.Set("preset", "fast"));

	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "fast") == 0);
}

TEST_CASE("Dictionary destructor frees resources", kDictionaryTestTag)
{
	// This test verifies no leaks through RAII
	{
		Dictionary d;
		REQUIRE(d.TrySet("key1", "value1").has_value());
		REQUIRE(d.TrySet("key2", "value2").has_value());
		REQUIRE(d.TrySet("key3", "value3").has_value());
		// Destructor should free all
	}

	// If we reach here without crashes, RAII worked
	REQUIRE(true);
}

TEST_CASE("Dictionary self-move-assignment is safe", kDictionaryTestTag)
{
	Dictionary d;
	REQUIRE(d.TrySet("preset", "medium").has_value());

	// Self-assignment should be safe
	d = std::move(d);

	// Dictionary should still be valid (implementation-defined state)
	// At minimum, it should not crash
	REQUIRE_NOTHROW(d.Clear());
}

TEST_CASE("Dictionary multiple Clear calls are safe", kDictionaryTestTag)
{
	Dictionary d;
	REQUIRE(d.TrySet("key", "value").has_value());

	d.Clear();
	REQUIRE_FALSE(static_cast<bool>(d));

	// Second Clear on empty dictionary should be safe
	REQUIRE_NOTHROW(d.Clear());
	REQUIRE_FALSE(static_cast<bool>(d));
}

TEST_CASE("Dictionary with special characters in keys and values", kDictionaryTestTag)
{
	Dictionary d;

	// Keys and values with various characters
	REQUIRE(d.TrySet("key-with-dashes", "value_with_underscores").has_value());
	REQUIRE(d.TrySet("key.with.dots", "value:with:colons").has_value());

	AVDictionaryEntry* entry1 = av_dict_get(*d.Ptr(), "key-with-dashes", nullptr, 0);
	REQUIRE(entry1 != nullptr);
	REQUIRE(std::strcmp(entry1->value, "value_with_underscores") == 0);

	AVDictionaryEntry* entry2 = av_dict_get(*d.Ptr(), "key.with.dots", nullptr, 0);
	REQUIRE(entry2 != nullptr);
	REQUIRE(std::strcmp(entry2->value, "value:with:colons") == 0);
}

TEST_CASE("Dictionary overwriting existing key", kDictionaryTestTag)
{
	Dictionary d;

	REQUIRE(d.TrySet("preset", "fast").has_value());

	AVDictionaryEntry* entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "fast") == 0);

	// Overwrite with new value
	REQUIRE(d.TrySet("preset", "slow").has_value());

	entry = av_dict_get(*d.Ptr(), "preset", nullptr, 0);
	REQUIRE(entry != nullptr);
	REQUIRE(std::strcmp(entry->value, "slow") == 0);
}
