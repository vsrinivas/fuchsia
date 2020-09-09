// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <memory>

namespace {

// Lines of text for each result are prefixed with this.
constexpr const char* kTestOutputPrefix = "  - ";

// The number of warm up iterations prior to test runs.
constexpr unsigned kWarmUpIterations = 5;

// The number of test runs to do.
constexpr unsigned kNumTestRuns = 10;

// Kilobyte.
constexpr unsigned kKb = 1024;

// Megabyte.
constexpr unsigned kMb = kKb * kKb;

unsigned SizeValue(unsigned size) {
  if (size >= kMb) {
    return size / kMb;
  }
  if (size >= kKb) {
    return size / kKb;
  }
  return size;
}

const char* SizeSuffix(unsigned size) {
  if (size >= kMb) {
    return "MiB";
  }
  if (size >= kKb) {
    return "KiB";
  }
  return "B";
}

// Measures how long it takes to run some number of iterations of a closure.
// Returns a value in microseconds.
template <typename T>
float Measure(unsigned iterations, const T& closure) {
  zx_ticks_t start = zx_ticks_get();
  for (unsigned i = 0; i < iterations; ++i) {
    closure();
  }
  zx_ticks_t stop = zx_ticks_get();
  return (static_cast<float>(stop - start) * 1000000.f / static_cast<float>(zx_ticks_per_second()));
}

// Runs a closure repeatedly and prints its timing.
template <typename T>
void RunAndMeasure(const char* test_name, unsigned iterations, const T& closure) {
  printf("\n* %s ...\n", test_name);

  float warm_up_time = Measure(kWarmUpIterations, closure);

  printf("%swarm-up: %u iterations in %.3f us, %.3f us per iteration\n", kTestOutputPrefix,
         kWarmUpIterations, warm_up_time, warm_up_time / kWarmUpIterations);

  float run_times[kNumTestRuns];
  for (unsigned i = 0; i < kNumTestRuns; ++i) {
    run_times[i] = Measure(iterations, closure);
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }

  float min = 0, max = 0;
  float cumulative = 0;
  for (const auto rt : run_times) {
    if (min == 0 || min > rt) {
      min = rt;
    }
    if (max == 0 || max < rt) {
      max = rt;
    }
    cumulative += rt;
  }
  float average = cumulative / kNumTestRuns;

  printf("%srun: %u test runs, %u iterations per run\n", kTestOutputPrefix, kNumTestRuns,
         iterations);
  printf("%stotal (usec): min: %.3f, max: %.3f, ave: %.3f\n", kTestOutputPrefix, min, max, average);
  printf("%sper-iteration (usec): min: %.3f\n",
         // The static cast is to avoid a "may change value" warning.
         kTestOutputPrefix, min / static_cast<float>(iterations));
}

void RunPingPongBenchmark(llcpp::fuchsia::hardware::goldfish::Pipe::SyncClient& pipe, unsigned size,
                          unsigned iterations) {
  {
    auto result = pipe.SetBufferSize(size);
    ZX_ASSERT(result.ok() && result.value().res == ZX_OK);
  }

  zx::vmo vmo;
  {
    auto result = pipe.GetBuffer();
    ZX_ASSERT(result.ok() && result.value().res == ZX_OK);
    vmo = std::move(result.value().vmo);
  }

  {
    auto buffer = std::make_unique<uint8_t[]>(size);
    uint8_t* data = buffer.get();
    memset(data, 0xff, size);
    vmo.write(data, 0, size);
  }

  char test_name[64];
  snprintf(test_name, sizeof(test_name), "pingpong, %u%s", SizeValue(size), SizeSuffix(size));

  RunAndMeasure(test_name, iterations, [&pipe, size] {
    auto result = pipe.DoCall(size, 0, size, 0);
    // For the test purpose we expect the buffer is small enough
    // so that we can finish in one write-read round trip.
    ZX_ASSERT(result.ok() && result.value().res == ZX_OK);
    ZX_ASSERT(result.value().actual == 2 * size);
  });
}

}  // namespace

int main(int argc, char** argv) {
  int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
  ZX_ASSERT(fd >= 0);

  zx::channel channel;
  ZX_ASSERT(fdio_get_service_handle(fd, channel.reset_and_get_address()) == ZX_OK);

  zx::channel pipe_client;
  zx::channel pipe_server;
  ZX_ASSERT(zx::channel::create(0, &pipe_client, &pipe_server) == ZX_OK);

  llcpp::fuchsia::hardware::goldfish::PipeDevice::SyncClient pipe_device(std::move(channel));
  ZX_ASSERT(pipe_device.OpenPipe(std::move(pipe_server)).ok());

  llcpp::fuchsia::hardware::goldfish::Pipe::SyncClient pipe(std::move(pipe_client));
  zx::vmo vmo;

  {
    auto result = pipe.GetBuffer();
    ZX_ASSERT(result.ok() && result.value().res == ZX_OK);
    vmo = std::move(result.value().vmo);
  }

  // Connect to pingpong service.
  constexpr char kPipeName[] = "pipe:pingpong";
  size_t bytes = strlen(kPipeName) + 1;
  ZX_ASSERT(vmo.write(kPipeName, 0, bytes) == ZX_OK);

  {
    auto result = pipe.Write(bytes, 0);
    ZX_ASSERT(result.ok() && result.value().res == ZX_OK);
    ZX_ASSERT(result.value().actual == bytes);
  }

  if (argc > 1) {
    for (int i = 1; (i + 1) < argc; i += 2) {
      unsigned size = atoi(argv[i]);
      unsigned iterations = atoi(argv[i + 1]);

      RunPingPongBenchmark(pipe, size, iterations);
    }
  } else {
    RunPingPongBenchmark(pipe, ZX_PAGE_SIZE, 500);
    RunPingPongBenchmark(pipe, kMb, 5);
  }

  printf("\nGoldfish benchmarks completed.\n");

  return 0;
}
