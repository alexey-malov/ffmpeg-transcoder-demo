#include <catch2/catch_test_macros.hpp>

import mm_pipeline.async_queue;

using namespace mm_pipeline;

TEST_CASE("TryPush / TryPop basic", "[AsyncQueue]")
{
	AsyncQueue<int> q(2);

	REQUIRE(q.TryPush(1) == AsyncQueue<int>::TryPushResult::Ok);
	REQUIRE(q.TryPush(2) == AsyncQueue<int>::TryPushResult::Ok);
	REQUIRE(q.TryPush(3) == AsyncQueue<int>::TryPushResult::Full);

	auto v1 = q.TryPop();
	REQUIRE(v1);
	REQUIRE(*v1 == 1);

	auto v2 = q.TryPop();
	REQUIRE(v2);
	REQUIRE(*v2 == 2);

	auto v3 = q.TryPop();
	REQUIRE(!v3);
	REQUIRE(v3.error() == AsyncQueue<int>::TryPopError::Empty);
}

TEST_CASE("Close behavior", "[AsyncQueue]")
{
	AsyncQueue<int> q(2);

	REQUIRE(q.TryPush(1) == AsyncQueue<int>::TryPushResult::Ok);

	q.Close();

	REQUIRE(q.TryPush(2) == AsyncQueue<int>::TryPushResult::Closed);

	auto v1 = q.TryPop();
	REQUIRE(v1);
	REQUIRE(*v1 == 1);

	auto v2 = q.TryPop();
	REQUIRE(!v2);
	REQUIRE(v2.error() == AsyncQueue<int>::TryPopError::Closed);
}

TEST_CASE("Blocking Push/Pop", "[AsyncQueue]")
{
	AsyncQueue<int> q(1);
	std::stop_source ss;

	std::thread producer([&] {
		q.PushOrWait(42, ss.get_token());
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	auto val = q.PopOrWait(ss.get_token());
	REQUIRE(val == 42);

	producer.join();
}

TEST_CASE("PopOrWait stops with stop_token", "[AsyncQueue]")
{
	AsyncQueue<int> q(1);
	std::stop_source ss;

	std::thread t([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		ss.request_stop();
	});

	REQUIRE_THROWS_AS(q.PopOrWait(ss.get_token()), std::exception);

	t.join();
}

TEST_CASE("PopAllOrWait returns batch", "[AsyncQueue]")
{
	AsyncQueue<int> q(10);
	std::stop_source ss;

	for (int i = 0; i < 5; ++i)
		REQUIRE(q.TryPush(i) == AsyncQueue<int>::TryPushResult::Ok);

	std::deque<int> out;

	q.PopAllOrWait(out, ss.get_token());

	REQUIRE(out.size() == 5);

	for (int i = 0; i < 5; ++i)
		REQUIRE(out[i] == i);

	auto empty = q.TryPop();
	REQUIRE(!empty);
}

TEST_CASE("TryPopAll", "[AsyncQueue]")
{
	AsyncQueue<int> q(10);

	for (int i = 0; i < 3; ++i)
		q.TryPush(i);

	std::deque<int> out;

	size_t n = q.TryPopAll(out);

	REQUIRE(n == 3);
	REQUIRE(out.size() == 3);

	auto empty = q.TryPop();
	REQUIRE(!empty);
}

TEST_CASE("PopUpToOrWait respects limit", "[AsyncQueue]")
{
	AsyncQueue<int> q(10);
	std::stop_source ss;

	for (int i = 0; i < 5; ++i)
		q.TryPush(i);

	std::deque<int> out;

	size_t n = q.PopUpToOrWait(out, 3, ss.get_token());

	REQUIRE(n == 3);
	REQUIRE(out.size() == 3);

	for (int i = 0; i < 3; ++i)
		REQUIRE(out[i] == i);

	auto rest = q.TryPop();
	REQUIRE(rest);
}

TEST_CASE("Producer/Consumer multithread", "[AsyncQueue]")
{
	AsyncQueue<int> q(100);
	std::stop_source ss;

	constexpr int N = 10000;
	std::atomic<int> sum{ 0 };

	std::jthread producer([&] {
		for (int i = 1; i <= N; ++i)
		{
			while (q.TryPush(i) != AsyncQueue<int>::TryPushResult::Ok)
				std::this_thread::yield();
		}
		q.Close();
	});

	std::jthread consumer([&] {
		while (true)
		{
			auto v = q.TryPop();
			if (!v)
			{
				if (v.error() == AsyncQueue<int>::TryPopError::Closed)
					break;

				std::this_thread::yield();
				continue;
			}
			sum += *v;
		}
	});

	producer.join();
	consumer.join();

	REQUIRE(sum == (N * (N + 1)) / 2);
}