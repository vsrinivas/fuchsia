// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <ratio>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "action.h"
#include "object.h"
#include "src/lib/fxl/logging.h"
#include "utility.h"
#include "worker.h"
#include "workload.h"

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

constexpr char kShortOptions[] = "hfiltv::";

// clang-format off
constexpr struct option kLongOptions[] = {
    { "help",       no_argument,        nullptr, 'h' },
    { "file",       required_argument,  nullptr, 'f' },
    { "interval",   required_argument,  nullptr, 'i' },
    { "list",       no_argument,        nullptr, 'l' },
    { "terse",      no_argument,        nullptr, 't' },
    { "verbose",    no_argument,        nullptr, 'v' },
    { nullptr, 0, nullptr, 0 }
};
// clang-format on

constexpr char kDefaultWorkloadPath[] = "/pkg/data/default.json";
constexpr auto kDefaultWorkloadInterval = 10s;

void PrintUsage(const char* program_name) {
  printf(
      "Usage: %s [-hfltv] [--help] [--file <PATH>] [--interval <INTERVAL>] [--list] [--terse] "
      "[--verbose]\n"
      "Executes a synthetic workload and reports benchmarks.\n"
      "With --help or -h, display this help and exit.\n"
      "With --file <PATH> or -f <PATH>, execute the workload file given by PATH.\n"
      "With --interval <INTERVAL> or -i <INTERVAL>, run workload for <INTERVAL> time.\n"
      "With --list or -l, list workload files included in this package.\n"
      "With --terse or -t, show simplified output.\n"
      "With --verbose or -v, show verbose output.\n"
      "\n"
      "The default workload file is: %s\n"
      "The default workload interval is %llu seconds, unless specified in the\n"
      "workload config or using --interval.\n",
      program_name, kDefaultWorkloadPath, kDefaultWorkloadInterval.count());
}

int main(int argc, char** argv) {
  std::optional<std::chrono::nanoseconds> default_interval;
  std::string workload_path = kDefaultWorkloadPath;
  bool help_flag = false;
  bool list_flag = false;
  bool terse_flag = false;
  bool verbose_flag = false;

  int opt;
  while ((opt = getopt_long(argc, argv, kShortOptions, kLongOptions, nullptr)) != -1) {
    switch (opt) {
      case 'h':
        help_flag = true;
        break;
      case 'l':
        list_flag = true;
        break;
      case 't':
        terse_flag = true;
        break;
      case 'v':
        verbose_flag = true;
        break;
      case 'i':
        default_interval = ParseDurationString(optarg);
        break;
      case 'f':
        workload_path = optarg;
        break;
      default:
        PrintUsage(argv[0]);
        return 1;
    }
  }

  if (help_flag) {
    PrintUsage(argv[0]);
    return 0;
  }

  std::cout << "Loading workload config from: " << workload_path << std::endl;
  Workload workload = Workload::Load(workload_path);

  if (workload.priority().has_value()) {
    auto profile = GetProfile(workload.priority().value());
    const auto status = zx::thread::self()->set_profile(*profile, 0);
    FXL_CHECK(status == ZX_OK) << "Failed to set the priority of the main thread!";
  }

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<Worker>> workers;
  for (auto& worker_config : workload.workers()) {
    auto [thread, worker] = Worker::Create(std::move(worker_config));
    threads.emplace_back(std::move(thread));
    workers.emplace_back(std::move(worker));
  }

  // Setup to sample CPU stats.
  const size_t cpu_count = ReadCpuCount();
  std::vector<zx_info_cpu_stats_t> cpu_stats_start(cpu_count);
  std::vector<zx_info_cpu_stats_t> cpu_stats_end(cpu_count);

  std::cout << "Waiting for workers to start up..." << std::endl;
  Worker::WaitForAllReady(threads.size());

  std::cout << "Kicking off workload..." << std::endl;
  Worker::StartAll();

  ReadCpuStats(cpu_stats_start.data(), cpu_stats_start.size());

  const std::chrono::nanoseconds interval_ns =
      default_interval.has_value() ? default_interval.value()
                                   : workload.interval().has_value() ? workload.interval().value()
                                                                     : kDefaultWorkloadInterval;
  const auto interval_s = double_seconds{interval_ns}.count();
  std::cout << "Waiting for " << interval_s << " s..." << std::endl;
  std::this_thread::sleep_for(interval_ns);

  ReadCpuStats(cpu_stats_end.data(), cpu_stats_end.size());

  std::cout << "Terminating workload..." << std::endl;
  Worker::TerminateAll();

  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();

  std::cout << "CPU Stats:" << std::endl;
  for (size_t i = 0; i < cpu_stats_start.size(); i++) {
    const std::chrono::nanoseconds idle_time_ns{cpu_stats_end[i].idle_time -
                                                cpu_stats_start[i].idle_time};

    const auto idle_time_s = double_seconds{idle_time_ns}.count();
    const auto active_time_s = interval_s - idle_time_s;

    std::cout << "  CPU " << i << ":" << std::endl;
    std::cout << "    Average Utilization: " << active_time_s << " s ("
              << (active_time_s * 100.0 / interval_s) << "%)" << std::endl;
  }

  struct GroupStats {
    size_t count{0};
    uint64_t iterations{0};
    std::chrono::nanoseconds runtime{0};

    uint64_t AverageIterations() const { return iterations / count; };
    double_seconds AverageRuntime() const { return double_seconds{runtime / count}; }
  };

  std::map<std::string, GroupStats> group_stats;

  for (auto& worker : workers) {
    if (verbose_flag) {
      worker->Dump();
    }

    // Add an entry for each unique group.
    auto [iter, okay] = group_stats.emplace(worker->group(), GroupStats{});

    // Accumulate group stats.
    iter->second.count++;
    iter->second.iterations += worker->spin_iterations();
    iter->second.runtime += worker->total_runtime();
  }
  workers.clear();

  std::cout << "Group stats:" << std::endl;
  for (auto& group : group_stats) {
    const auto& group_name = group.first;
    const auto thread_count = group.second.count;
    const auto average_iterations = group.second.AverageIterations();
    const auto average_runtime = group.second.AverageRuntime().count();

    std::cout << "Group: " << group_name << std::endl;
    std::cout << "  Threads: " << thread_count << std::endl;
    std::cout << "  Average Iterations: " << average_iterations << " per thread ("
              << (average_iterations * thread_count / cpu_count) << " per cpu)" << std::endl;
    std::cout << "  Average Runtime: " << average_runtime << " s/thread ("
              << (average_runtime * thread_count / cpu_count) << " s/cpu)" << std::endl;
  }

  using MapItem = decltype(group_stats)::value_type;
  std::vector<std::reference_wrapper<MapItem>> group_list{group_stats.begin(), group_stats.end()};
  std::sort(group_list.begin(), group_list.end(),
            [](const MapItem& a, const MapItem& b) { return a.second.runtime > b.second.runtime; });

  std::cout << "Relative stats:" << std::endl;
  for (auto i = group_list.cbegin(); i != group_list.cend(); ++i) {
    for (auto j = i + 1; j != group_list.cend(); ++j) {
      const MapItem& group_a = *i;
      const MapItem& group_b = *j;

      std::cout << "Group " << group_a.first << " vs " << group_b.first << std::endl;

      const auto runtime_a = group_a.second.AverageRuntime();
      const auto runtime_b = group_b.second.AverageRuntime();
      std::cout << "  Relative Runtime: "
                << 100.0 * (runtime_a - runtime_b) / (runtime_a + runtime_b) << " %" << std::endl;
    }
  }

  std::cout << "Done!" << std::endl;
  return 0;
}
