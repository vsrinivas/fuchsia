// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT.

#include <fcntl.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <getopt.h>
#include <lib/service/llcpp/service.h>
#include <lib/stdcompat/array.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/clock.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/profile.h>

#include <climits>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <ostream>
#include <thread>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>

#include "user-copy.h"

namespace {

constexpr size_t kExtraPadding = 16;

constexpr int kSampleCount = 30;

void RandomizeBuffers(cpp20::span<uint8_t> buffer, unsigned int& seed) {
  for (auto& b : buffer) {
    b = static_cast<uint8_t>(rand_r(&seed));
  }
}

// Relative time since a platform defined event. Use this to calculate the delta between times.
[[gnu::always_inline, maybe_unused]] int64_t GetClockTime() {
  return zx::clock::get_monotonic().get();
}

// Platform specific logic to bind thread execution to a particular cpu base on index.
[[maybe_unused]] bool BindToCpu(int cpu_num, bool use_deadline) {
  auto client_or = service::Connect<fuchsia_kernel::RootJob>();
  if (!client_or.is_ok()) {
    printf("Failed to connect to %s\n.", fidl::DiscoverableProtocolName<fuchsia_kernel::RootJob>);
    return false;
  }
  fidl::WireSyncClient<fuchsia_kernel::RootJob> client;
  client.Bind(std::move(client_or).value());
  auto res = client->Get();
  if (!res.ok()) {
    printf("Failed to obtain root job handle.\n");
    return false;
  }

  zx::job root_job = std::move(res.value()).job;
  zx_profile_info_t profile_info = {};

  profile_info.flags = ZX_PROFILE_INFO_FLAG_CPU_MASK;
  profile_info.cpu_affinity_mask.mask[cpu_num / ZX_CPU_SET_BITS_PER_WORD] =
      1 << (cpu_num % ZX_CPU_SET_BITS_PER_WORD);

  if (use_deadline) {
    profile_info.flags |= ZX_PROFILE_INFO_FLAG_DEADLINE;
    profile_info.deadline_params = {
        .capacity = zx::msec(1).get(),
        .relative_deadline = zx::msec(5).get(),
        .period = zx::msec(15).get(),
    };
  }

  zx::profile profile;
  if (auto res = zx::profile::create(root_job, 0, &profile_info, &profile); res != ZX_OK) {
    printf("Failed to create profile with error %s.\n", zx_status_get_string(res));
    return false;
  }

  auto curr_thread = zx::thread::self();
  if (auto res = curr_thread->set_profile(profile, 0); res != ZX_OK) {
    printf("Failed to set thread profile with error %s.\n", zx_status_get_string(res));
    return false;
  }

  // We are finally done.
  return true;
}

template <bool using_deadline>
int64_t SampleCopy(int cpu_num, size_t block_size, size_t src_alignment, size_t dst_alignment,
                   unsigned int& seed) {
  std::string error_str = std::string("SampleCopy{\n") + "  size: " + std::to_string(block_size) +
                          "\n" + "  src_alignment: " + std::to_string(src_alignment) + "\n" +
                          "  dst_alignment: " + std::to_string(dst_alignment) + "\n" +
                          "  seed: " + std::to_string(seed) + "\n" + "}\n";

  std::unique_ptr<uint8_t[]> src_raw(new (std::align_val_t(16))
                                         uint8_t[block_size + 2 * kExtraPadding + src_alignment]);
  std::unique_ptr<uint8_t[]> dst_raw(new (std::align_val_t(16))
                                         uint8_t[block_size + 2 * kExtraPadding + dst_alignment]);

  auto src = cpp20::span(src_raw.get(), 2 * kExtraPadding + src_alignment + block_size);
  RandomizeBuffers(src, seed);

  auto dst = cpp20::span(dst_raw.get(), 2 * kExtraPadding + dst_alignment + block_size);
  RandomizeBuffers(dst, seed);

  // Keep the original contents for verification after sampling.
  std::vector<uint8_t> original_src(src.begin(), src.end());
  std::vector<uint8_t> original_dst(dst.begin(), dst.end());

  auto src_copy = src.subspan(kExtraPadding + src_alignment, block_size);
  auto dst_copy = dst.subspan(kExtraPadding + dst_alignment, block_size);
  int64_t elapsed = 0;
  std::thread sampler(
      [cpu_num](auto src, auto dst, size_t size, int64_t* elapsed) {
        if (!BindToCpu(cpu_num, using_deadline)) {
          printf("Failed to bind CPU to core %d.\n", cpu_num);
          return;
        }
        // Warm up
        for (int i = 0; i < 10; ++i) {
          uint64_t fault_return = 0;
          std::ignore = ARM64_USERCOPY_FN(dst.data(), src.data(), size, &fault_return, 0);
        }

        // If using deadline, then we yield before the sampling begins. So that the measurements
        // begin at the start of the
        if constexpr (using_deadline) {
          zx_thread_legacy_yield(0);
        }

        // 30 averaged samples.
        auto start = GetClockTime();
        for (int i = 0; i < kSampleCount; ++i) {
          uint64_t fault_return = 0;
          std::ignore = ARM64_USERCOPY_FN(dst.data(), src.data(), size, &fault_return, 0);
        }
        *elapsed = (GetClockTime() - start) / kSampleCount;
      },
      src_copy, dst_copy, block_size, &elapsed);

  sampler.join();

  // src unmodified
  ZX_ASSERT_MSG(memcmp(original_src.data(), src.data(), src.size()) == 0, "%s", error_str.c_str());

  // dst range copied correctly.
  ZX_ASSERT_MSG(memcmp(dst_copy.data(), src_copy.data(), dst_copy.size()) == 0, "%s",
                error_str.c_str());

  // dst ranges not part of the copy range untouched.
  ZX_ASSERT_MSG(memcmp(original_dst.data(), dst.data(), kExtraPadding + dst_alignment) == 0, "%s",
                error_str.c_str());
  ZX_ASSERT_MSG(
      memcmp(original_dst.data() + dst_alignment + kExtraPadding + block_size,
             dst.subspan(kExtraPadding + dst_alignment + block_size).data(), kExtraPadding) == 0,
      "%s", error_str.c_str());

  return elapsed;
}

constexpr auto kBlockSize =
    cpp20::to_array<size_t>({1,  2,  3,   4,   8,   15,  16,  31,  32,  63,   64,   95,
                             96, 97, 127, 128, 255, 256, 257, 511, 512, 1023, 1024, 2048});

constexpr auto kAlignments = cpp20::to_array<size_t>({0, 1, 7, 8, 9, 15});

}  // namespace

int main(int argc, char** argv) {
  auto long_opts = cpp20::to_array<struct option>({
      {"cpu", required_argument, nullptr, 'c'},
      {"output", required_argument, nullptr, 'o'},
      {"seed", optional_argument, nullptr, 's'},
      {"profile", optional_argument, nullptr, 'p'},
      {"cpu_name", required_argument, nullptr, 'n'},
      {nullptr, 0, nullptr, 0},
  });

  std::string output_path;
  unsigned int seed = static_cast<unsigned int>(time(nullptr));
  [[maybe_unused]] int cpu = -1;
  std::string cpu_name;
  bool done = false;
  bool use_deadline_profile = false;

  while (const int ch = getopt_long(argc, argv, "p:c:o:s:n:", long_opts.data(), nullptr)) {
    switch (ch) {
      case 'o':
        output_path = optarg;
        break;
      case 's':
        seed = atoi(optarg);
        break;
      case 'c':
        cpu = atoi(optarg);
        break;
      case 'p':
        use_deadline_profile = memcmp(optarg, "deadline", strlen(optarg)) == 0;
        break;
      case 'n':
        cpu_name = optarg;
        break;

      default:
        done = true;
        break;
    }

    if (done) {
      break;
    }
  }

  if (output_path.empty() || cpu < 0) {
    printf(
        R"""([OPTIONS]

--cpu_name,-n STRING    Used as a name for the cpu to use in the csv output.

--cpu,-c      UINT      Fixes the CPU to bind to, for running the benchmark.

--output,-o   PATH      Sets the output path where results will be written in csv format.

--seed,-s     UINT      Fixes the seed to use for randomizing buffer contents.

--profile,-p  TYPE      Fixes the profile to use for sampling.
                        TYPE must be default or deadline.
)""");
    return -1;
  }

  printf("Benchmark Params:\n cpu: %d\n cpu_name: %s\n profile: %s\n output: %s\n seed: %d\n", cpu,
         cpu_name.c_str(), use_deadline_profile ? "deadline" : "default", output_path.c_str(),
         seed);

  fbl::unique_fd output_fd(open(output_path.c_str(), O_WRONLY | O_CREAT | O_APPEND));
  if (!output_fd) {
    printf("Failed to open file %s\n", strerror(errno));
    return -1;
  }

  auto puts = [&output_fd](const std::string& str) {
    size_t written = 0;
    while (written < str.length()) {
      auto res = write(output_fd.get(), str.c_str(), str.size());
      ZX_ASSERT_MSG(res >= 0, "%s", strerror(errno));
      written += res;
    }
  };

  puts("variant_name,cpu_name,block_size,src_alignment,dst_alignment,time\n");

  for (auto block_size : kBlockSize) {
    printf("Sampling: Block size %zu bytes for all alignments.\n", block_size);
    for (auto src_alignment : kAlignments) {
      for (auto dst_alignment : kAlignments) {
        auto sampler = (use_deadline_profile) ? SampleCopy<true> : SampleCopy<false>;
        auto sample = sampler(cpu, block_size, src_alignment, dst_alignment, seed);
        puts(std::string(argv[0]) + "," + cpu_name + "," + std::to_string(block_size) + "," +
             std::to_string(src_alignment) + "," + std::to_string(dst_alignment) + "," +
             std::to_string(sample) + "\n");
      }
    }
  }

  return 0;
}
