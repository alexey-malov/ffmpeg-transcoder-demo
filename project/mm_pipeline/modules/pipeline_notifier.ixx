export module mm_pipeline.pipeline_notifier;

import std;

namespace mm_pipeline
{

export struct PipelineNotifier
{
	using Clock = std::chrono::high_resolution_clock;
	using Duration = Clock::duration;

	Duration totalWaitTime = Duration::zero();

	std::mutex mutex;
	std::condition_variable cv;
	std::atomic_uint64_t epoch = 0;

	void Notify()
	{
		epoch.fetch_add(1, std::memory_order_relaxed);
		cv.notify_one();
	}

	void Wait(std::uint64_t oldEpoch)
	{
		if (epoch.load(std::memory_order_relaxed) != oldEpoch)
			return;
		const auto start = Clock::now();
		std::unique_lock lock{ mutex };
		cv.wait(lock, [&] {
			return epoch.load(std::memory_order_relaxed) != oldEpoch;
		});
		const auto end = Clock::now();
		totalWaitTime += end - start;
	}
};

} // namespace mm_pipeline