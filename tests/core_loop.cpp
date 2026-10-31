// SPDX-License-Identifier: Apache-2.0
//
// core_loop.cpp -- btop-aix (2026-10-19): Core::Loop (src/btop_core.cpp) with a fake collector: snapshots
// advance, commands run on the core thread in order, stop() is idempotent, call() after stop() throws.

#include <atomic>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "btop_core.hpp"

TEST(CoreLoop, SnapshotsAndCommands) {
	std::atomic<std::thread::id> collector_thread{};
	Core::Loop loop(5, [&](uint64_t seq) {
		collector_thread = std::this_thread::get_id();
		Core::Snapshot s;
		s.seq = seq;
		return s;
	});
	EXPECT_EQ(loop.snapshot(), nullptr);
	loop.start();
	ASSERT_TRUE(loop.wait_for_seq(3, 2000));
	EXPECT_GE(loop.snapshot()->seq, 3u);

	const auto cmd_thread = loop.call([] { return std::this_thread::get_id(); });
	EXPECT_EQ(cmd_thread, collector_thread.load()) << "commands run on the core thread";
	EXPECT_NE(cmd_thread, std::this_thread::get_id());

	std::vector<int> order;
	for (int i = 0; i < 5; ++i) loop.call([&order, i] { order.push_back(i); });
	EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));

	EXPECT_THROW(loop.call([]() -> int { throw std::logic_error("inside"); }), std::logic_error)
		<< "exceptions of a command reach the caller";

	loop.stop();
	loop.stop();
	EXPECT_THROW(loop.call([] { return 1; }), std::runtime_error);
}

TEST(CoreLoop, ConcurrentCallers) {
	Core::Loop loop(1000, [](uint64_t seq) { Core::Snapshot s; s.seq = seq; return s; });
	loop.start();
	int counter = 0;	//? touched only on the core thread
	std::vector<std::thread> callers;
	for (int t = 0; t < 4; ++t)
		callers.emplace_back([&] { for (int i = 0; i < 100; ++i) loop.call([&counter] { ++counter; }); });
	for (auto& c : callers) c.join();
	EXPECT_EQ(loop.call([&counter] { return counter; }), 400);
	loop.stop();
}
