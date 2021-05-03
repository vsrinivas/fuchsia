// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/radar/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

#include <queue>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

namespace radarutil {

class RadarUtil {
 public:
  static zx_status_t Run(int argc, char** argv,
                         fidl::ClientEnd<fuchsia_hardware_radar::RadarBurstReaderProvider> device);

 private:
  using BurstReaderProvider = fuchsia_hardware_radar::RadarBurstReaderProvider;
  using BurstReader = fuchsia_hardware_radar::RadarBurstReader;

  static constexpr zx::duration kDefaultRunTime = zx::sec(10);
  static constexpr size_t kDefaultVmoCount = 10;
  static constexpr zx::duration kDefaultBurstProcessTime = zx::nsec(0);

  class EventHandler : public fidl::WireAsyncEventHandler<BurstReader> {
   public:
    explicit EventHandler(RadarUtil* parent) : parent_(parent) {}

    void OnBurst(fidl::WireResponse<BurstReader::OnBurst>* event) override {
      parent_->OnBurst(event);
    }

    void Unbound(fidl::UnbindInfo info) override { parent_->Unbound(info); }

   private:
    RadarUtil* const parent_;
  };

  RadarUtil() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  zx_status_t ParseArgs(int argc, char** argv);
  zx_status_t ConnectToDevice(fidl::ClientEnd<BurstReaderProvider> device);
  zx_status_t RegisterVmos();
  zx_status_t UnregisterVmos();
  zx_status_t Run();

  int WorkerThread();

  void OnBurst(fidl::WireResponse<BurstReader::OnBurst>* event);
  void Unbound(fidl::UnbindInfo info) {}

  async::Loop loop_;
  fidl::Client<BurstReader> client_;
  zx::duration run_time_ = kDefaultRunTime;
  size_t vmo_count_ = kDefaultVmoCount;
  zx::duration burst_process_time_ = kDefaultBurstProcessTime;

  fbl::Mutex lock_;
  fbl::ConditionVariable worker_event_;
  std::queue<uint32_t> burst_vmo_ids_ TA_GUARDED(lock_);
  std::atomic_bool run_ = true;

  uint64_t bursts_received_ = 0;
  uint64_t burst_errors_ = 0;
};

}  // namespace radarutil
