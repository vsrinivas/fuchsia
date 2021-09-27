// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.radar/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/threads.h>

#include <optional>
#include <queue>

#include <fbl/array.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

namespace radarutil {

class RadarUtil : public fidl::WireAsyncEventHandler<fuchsia_hardware_radar::RadarBurstReader> {
 public:
  class FileProvider {
   public:
    virtual FILE* OpenFile(const char* path) const = 0;
  };

  class DefaultFileProvider : public FileProvider {
   public:
    FILE* OpenFile(const char* path) const override { return fopen(path, "w+"); }
  };

  static constexpr DefaultFileProvider kDefaultFileProvider;

  static zx_status_t Run(int argc, char** argv,
                         fidl::ClientEnd<fuchsia_hardware_radar::RadarBurstReaderProvider> device,
                         const FileProvider* file_provider = &kDefaultFileProvider);

 private:
  using BurstReaderProvider = fuchsia_hardware_radar::RadarBurstReaderProvider;
  using BurstReader = fuchsia_hardware_radar::RadarBurstReader;

  static constexpr zx::duration kDefaultRunTime = zx::sec(1);
  static constexpr size_t kDefaultVmoCount = 10;
  static constexpr zx::duration kDefaultBurstProcessTime = zx::nsec(0);
  // Indicates a burst error from the callback filling the queue to the worker draining it.
  static constexpr uint32_t kInvalidVmoId = UINT32_MAX;

  RadarUtil() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  ~RadarUtil() override;

  fidl::AnyTeardownObserver teardown_observer();

  zx_status_t ParseArgs(int argc, char** argv, const FileProvider* file_provider);
  zx_status_t ConnectToDevice(fidl::ClientEnd<BurstReaderProvider> device);
  zx_status_t RegisterVmos();
  zx_status_t UnregisterVmos();
  zx_status_t Run();
  zx_status_t ReadBursts();

  void OnBurst(fidl::WireResponse<BurstReader::OnBurst>* event) override;
  void on_fidl_error(fidl::UnbindInfo info) override {}

  async::Loop loop_;
  fidl::WireSharedClient<BurstReader> client_;
  fbl::Array<uint8_t> burst_buffer_;
  sync_completion_t client_teardown_completion_;
  std::optional<zx::duration> run_time_;
  std::optional<uint64_t> burst_count_;
  size_t vmo_count_ = kDefaultVmoCount;
  zx::duration burst_process_time_ = kDefaultBurstProcessTime;
  FILE* output_file_ = nullptr;

  fbl::Mutex lock_;
  fbl::ConditionVariable worker_event_;
  std::vector<zx::vmo> burst_vmos_;
  std::queue<uint32_t> burst_vmo_ids_ TA_GUARDED(lock_);
  std::atomic_bool run_ = true;

  uint64_t bursts_received_ = 0;
  uint64_t burst_errors_ = 0;

  bool help_ = false;
};

}  // namespace radarutil
