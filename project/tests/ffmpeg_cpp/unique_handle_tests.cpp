#include <catch2/catch_test_macros.hpp>

import std;
import ffmpeg.unique_handle;

namespace
{

struct Dummy
{
	int value = 0;
};

// A stateless deleter that increments a global counter.
struct StatelessDeleter
{
	static inline int calls = 0;
	void operator()(Dummy* p) noexcept
	{
		++calls;
		delete p;
	}
};

// A stateful deleter that increments an external counter and exposes a flag.
struct StatefulDeleter
{
	int* calls = nullptr;
	bool flag = false;

	void operator()(Dummy* p) noexcept
	{
		if (calls)
			++(*calls);
		delete p;
	}
};

using HandleWithStatelessDeleter = ffmpeg::UniqueHandle<Dummy, StatelessDeleter>;
using HandleWithStatefulDeleter = ffmpeg::UniqueHandle<Dummy, StatefulDeleter>;

} // namespace

TEST_CASE("UniqueHandle default-constructs empty and is false", "[UniqueHandle]")
{
	HandleWithStatelessDeleter h;
	REQUIRE_FALSE(static_cast<bool>(h));
	REQUIRE(h.get() == nullptr);
}

TEST_CASE("UniqueHandle owns pointer and deletes on destruction", "[UniqueHandle]")
{
	StatelessDeleter::calls = 0;
	{
		HandleWithStatelessDeleter h(new Dummy{ 42 });
		REQUIRE(static_cast<bool>(h));
		REQUIRE(h->value == 42);
		REQUIRE((*h).value == 42);
		REQUIRE(StatelessDeleter::calls == 0);
	}
	REQUIRE(StatelessDeleter::calls == 1);
}

TEST_CASE("UniqueHandle reset deletes old and adopts new pointer", "[UniqueHandle]")
{
	StatelessDeleter::calls = 0;

	HandleWithStatelessDeleter h(new Dummy{ 1 });
	REQUIRE(StatelessDeleter::calls == 0);

	h.reset(new Dummy{ 2 });
	REQUIRE(StatelessDeleter::calls == 1);
	REQUIRE(h->value == 2);

	h.reset(nullptr);
	REQUIRE(StatelessDeleter::calls == 2);
	REQUIRE_FALSE(static_cast<bool>(h));
	REQUIRE(h.get() == nullptr);
}

TEST_CASE("UniqueHandle release relinquishes ownership without deleting", "[UniqueHandle]")
{
	StatelessDeleter::calls = 0;

	HandleWithStatelessDeleter h(new Dummy{ 7 });
	Dummy* raw = h.release();

	REQUIRE(raw != nullptr);
	REQUIRE_FALSE(static_cast<bool>(h));
	REQUIRE(h.get() == nullptr);
	REQUIRE(StatelessDeleter::calls == 0);

	// Caller is responsible now.
	delete raw;
	REQUIRE(StatelessDeleter::calls == 0);
}

TEST_CASE("UniqueHandle move-construct transfers ownership and deleter", "[UniqueHandle]")
{
	StatelessDeleter::calls = 0;

	HandleWithStatelessDeleter a(new Dummy{ 10 });
	auto* rawA = a.get();

	HandleWithStatelessDeleter b(std::move(a));

	REQUIRE_FALSE(static_cast<bool>(a));
	REQUIRE(a.get() == nullptr);

	REQUIRE(static_cast<bool>(b));
	REQUIRE(b.get() == rawA);
	REQUIRE(b->value == 10);

	// destruction deletes exactly once
}

TEST_CASE("UniqueHandle move-assign deletes old then takes new ownership", "[UniqueHandle]")
{
	StatelessDeleter::calls = 0;

	HandleWithStatelessDeleter a(new Dummy{ 1 });
	HandleWithStatelessDeleter b(new Dummy{ 2 });

	auto* rawA = a.get();
	auto* rawB = b.get();

	b = std::move(a);

	// b should have deleted its old pointer once
	REQUIRE(StatelessDeleter::calls == 1);

	REQUIRE_FALSE(static_cast<bool>(a)); // a is empty
	REQUIRE(a.get() == nullptr); // a's pointer is null

	REQUIRE(static_cast<bool>(b));
	REQUIRE(b.get() == rawA); // now b owns a's old pointer
	REQUIRE(b.get() != rawB);
	REQUIRE(b->value == 1);
}

TEST_CASE("UniqueHandle exposes stateful deleter via get_deleter()", "[UniqueHandle]")
{
	int calls = 0;

	HandleWithStatefulDeleter h(
		new Dummy{ 3 },
		StatefulDeleter{ .calls = &calls, .flag = false });

	REQUIRE(h.get_deleter().calls == &calls);
	REQUIRE(h.get_deleter().flag == false);

	h.get_deleter().flag = true;
	REQUIRE(h.get_deleter().flag == true);

	h.reset(new Dummy{ 4 }); // deletes old
	REQUIRE(calls == 1);
	REQUIRE(h->value == 4);

	h.reset(nullptr); // deletes new
	REQUIRE(calls == 2);
}

TEST_CASE("UniqueHandle swap swaps pointers and deleters", "[UniqueHandle]")
{
	int calls1 = 0;
	int calls2 = 0;

	HandleWithStatefulDeleter a(
		new Dummy{ 11 },
		StatefulDeleter{ .calls = &calls1, .flag = true });

	HandleWithStatefulDeleter b(
		new Dummy{ 22 },
		StatefulDeleter{ .calls = &calls2, .flag = false });

	auto* rawA = a.get();
	auto* rawB = b.get();

	a.swap(b);

	REQUIRE(a.get() == rawB);
	REQUIRE(b.get() == rawA);

	REQUIRE(a->value == 22);
	REQUIRE(b->value == 11);

	REQUIRE(a.get_deleter().calls == &calls2);
	REQUIRE(b.get_deleter().calls == &calls1);

	REQUIRE(a.get_deleter().flag == false);
	REQUIRE(b.get_deleter().flag == true);
}

TEST_CASE("UniqueHandle size sanity: stateless deleter should not add much overhead", "[UniqueHandle]")
{
	// Note: exact size depends on ABI and EBO support. We don't assert exact equality
	// to sizeof(void*) to avoid false failures. We assert it's "small".
	using H = HandleWithStatelessDeleter;

	REQUIRE(sizeof(H) <= sizeof(void*) * 2);
	REQUIRE(alignof(H) == alignof(void*)); // typical; if this fails, you can relax it
}
