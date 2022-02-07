// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "radarutil.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

namespace radarutil {

using BurstReaderProvider = fuchsia_hardware_radar::RadarBurstReaderProvider;
using BurstReader = fuchsia_hardware_radar::RadarBurstReader;
using StatusCode = fuchsia_hardware_radar::wire::StatusCode;

class FakeRadarDevice : public fidl::WireServer<BurstReader> {
 public:
  // The burst size of our only existing radar driver.
  static constexpr size_t kBurstSize = 23247;

  FakeRadarDevice() : loop_(&kAsyncLoopConfigNeverAttachToThread), provider_(*this) {
    EXPECT_OK(loop_.StartThread("radarutil-test-thread"));
    thrd_create_with_name(
        &worker_thread_,
        [](void* ctx) { return reinterpret_cast<FakeRadarDevice*>(ctx)->WorkerThread(); }, this,
        "radarutil-test-burst-thread");
  }
  ~FakeRadarDevice() override {
    run_ = false;
    thrd_join(worker_thread_, nullptr);
  }

  zx_status_t RunRadarUtil(
      std::vector<std::string> args,
      const RadarUtil::FileProvider* file_provider = &RadarUtil::kDefaultFileProvider) {
    std::unique_ptr<char*[]> arglist(new char*[args.size()]);
    for (size_t i = 0; i < args.size(); i++) {
      arglist[i] = args[i].data();
    }

    optind = 1;  // Reset getopt before the next call.
    return RadarUtil::Run(static_cast<int>(args.size()), arglist.get(), GetProvider(),
                          file_provider);
  }

  size_t GetRegisteredVmoCount() const { return registered_vmo_count_; }
  size_t GetBurstsSent() const { return bursts_sent_; }

  void Ok() const {
    EXPECT_EQ(registered_vmo_count_, unregistered_vmo_count_);
    EXPECT_TRUE(bursts_started_);
    EXPECT_TRUE(bursts_stopped_);
  }

  void SetErrorOnBurst(size_t burst) { error_burst_ = burst; }

  void GetBurstSize(GetBurstSizeRequestView request,
                    GetBurstSizeCompleter::Sync& completer) override {
    completer.Reply(kBurstSize);
  }

  void RegisterVmos(RegisterVmosRequestView request,
                    RegisterVmosCompleter::Sync& completer) override {
    if (request->vmo_ids.count() != request->vmos.count()) {
      completer.ReplyError(StatusCode::kInvalidArgs);
      return;
    }

    fbl::AutoLock lock(&lock_);
    for (size_t i = 0; i < request->vmo_ids.count(); i++) {
      if (registered_vmos_.count(request->vmo_ids[i]) != 0) {
        completer.ReplyError(StatusCode::kVmoAlreadyRegistered);
        return;
      }

      registered_vmos_.emplace(request->vmo_ids[i], RegisteredVmo{
                                                        .vmo = std::move(request->vmos[i]),
                                                        .locked = false,
                                                    });
    }

    registered_vmo_count_ += request->vmo_ids.count();

    completer.ReplySuccess();
  }

  void UnregisterVmos(UnregisterVmosRequestView request,
                      UnregisterVmosCompleter::Sync& completer) override {
    fidl::Arena allocator;
    fidl::VectorView<zx::vmo> vmos(allocator, request->vmo_ids.count());

    fbl::AutoLock lock(&lock_);
    for (size_t i = 0; i < request->vmo_ids.count(); i++) {
      auto it = registered_vmos_.find(request->vmo_ids[i]);
      if (it == registered_vmos_.end()) {
        completer.ReplyError(StatusCode::kVmoNotFound);
        return;
      }

      vmos[i] = std::move(it->second.vmo);
    }

    unregistered_vmo_count_ += request->vmo_ids.count();

    completer.ReplySuccess(vmos);
  }

  void StartBursts(StartBurstsRequestView request, StartBurstsCompleter::Sync& completer) override {
    send_bursts_ = true;
    bursts_started_ = true;
  }

  void StopBursts(StopBurstsRequestView request, StopBurstsCompleter::Sync& completer) override {
    send_bursts_ = false;
    bursts_stopped_ = true;
    completer.Reply();
  }

  void UnlockVmo(UnlockVmoRequestView request, UnlockVmoCompleter::Sync& completer) override {
    fbl::AutoLock lock(&lock_);
    auto it = registered_vmos_.find(request->vmo_id);
    if (it != registered_vmos_.end()) {
      it->second.locked = false;
    }
  }

 private:
  class FakeBurstReaderProvider : public fidl::WireServer<BurstReaderProvider> {
   public:
    explicit FakeBurstReaderProvider(FakeRadarDevice& parent) : parent_(parent) {}

    void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override {
      fidl::WireRequest<BurstReaderProvider::Connect> outgoing(std::move(request->server));
      parent_.Connect(&outgoing, completer);
    }

   private:
    FakeRadarDevice& parent_;
  };

  struct RegisteredVmo {
    zx::vmo vmo;
    bool locked;
  };

  void Connect(FakeBurstReaderProvider::ConnectRequestView request,
               FakeBurstReaderProvider::ConnectCompleter::Sync& completer) {
    server_.emplace(fidl::BindServer(loop_.dispatcher(), std::move(request->server), this));
    completer.ReplySuccess();
  }

  fidl::ClientEnd<BurstReaderProvider> GetProvider() {
    fidl::ClientEnd<BurstReaderProvider> client;
    fidl::ServerEnd<BurstReaderProvider> server;
    if (zx::channel::create(0, &client.channel(), &server.channel()) != ZX_OK) {
      return {};
    }

    fidl::BindServer(loop_.dispatcher(), std::move(server), &provider_);
    return client;
  }

  int WorkerThread() {
    fuchsia_hardware_radar::wire::RadarBurstReaderOnBurstResult result;
    bool sent_error = false;

    while (run_) {
      zx::nanosleep(zx::deadline_after(zx::usec(33'333)));

      if (!send_bursts_) {
        continue;
      }

      if (bursts_sent_++ == error_burst_) {
        SendBurstError(StatusCode::kSensorTimeout);
        sent_error = true;
      }

      std::optional<uint32_t> vmo = GetUnlockedVmo();
      if (vmo) {
        SendBurst(*vmo);
        sent_error = false;
      } else if (!sent_error) {
        SendBurstError(StatusCode::kOutOfVmos);
        sent_error = true;
      }
    }

    return thrd_success;
  }

  std::optional<uint32_t> GetUnlockedVmo() {
    fbl::AutoLock lock(&lock_);
    for (auto& pair : registered_vmos_) {
      if (!pair.second.locked) {
        pair.second.locked = true;
        pair.second.vmo.write(&bursts_sent_, 0, sizeof(bursts_sent_));
        return pair.first;
      }
    }

    return {};
  }

  void SendBurst(uint32_t vmo_id) {
    fuchsia_hardware_radar::wire::RadarBurstReaderOnBurstResult result;
    fuchsia_hardware_radar::wire::RadarBurstReaderOnBurstResponse response;
    response.burst.vmo_id = vmo_id;
    result.set_response(
        fidl::ObjectView<fuchsia_hardware_radar::wire::RadarBurstReaderOnBurstResponse>::
            FromExternal(&response));
    fidl::WireSendEvent(*server_)->OnBurst(result);
  }

  void SendBurstError(StatusCode status) {
    fuchsia_hardware_radar::wire::RadarBurstReaderOnBurstResult result;
    result.set_err(status);
    fidl::WireSendEvent(*server_)->OnBurst(result);
  }

  async::Loop loop_;
  FakeBurstReaderProvider provider_;
  std::optional<fidl::ServerBindingRef<BurstReader>> server_;
  fbl::Mutex lock_;
  std::map<uint32_t, RegisteredVmo> registered_vmos_ TA_GUARDED(lock_);
  std::atomic<bool> run_ = true;
  std::atomic<bool> send_bursts_ = false;
  thrd_t worker_thread_;

  size_t error_burst_ = SIZE_MAX;

  size_t registered_vmo_count_ = 0;
  size_t unregistered_vmo_count_ = 0;
  size_t bursts_sent_ = 0;
  bool bursts_started_ = false;
  bool bursts_stopped_ = false;
};

TEST(RadarUtilTest, RunTimed) {
  FakeRadarDevice device;
  EXPECT_OK(device.RunRadarUtil({"radarutil", "-t", "10s", "-v", "20", "-p", "1ms"}));
  EXPECT_EQ(device.GetRegisteredVmoCount(), 20);
  ASSERT_NO_FATAL_FAILURE(device.Ok());
}

TEST(RadarUtilTest, RunBurstCount) {
  FakeRadarDevice device;
  EXPECT_OK(device.RunRadarUtil({"radarutil", "-b", "50", "-v", "20", "-p", "1ms"}));
  EXPECT_EQ(device.GetRegisteredVmoCount(), 20);
  EXPECT_GE(device.GetBurstsSent(), 50);
  ASSERT_NO_FATAL_FAILURE(device.Ok());
}

TEST(RadarUtilTest, OutputToFile) {
  constexpr size_t kFileBufferSize = FakeRadarDevice::kBurstSize * 20;

  fbl::Array<uint8_t> file_buffer(new uint8_t[kFileBufferSize], kFileBufferSize);

  FILE* mem_file = fmemopen(file_buffer.get(), file_buffer.size(), "w");
  ASSERT_NOT_NULL(mem_file);

  class FakeFileProvider : public RadarUtil::FileProvider {
   public:
    explicit FakeFileProvider(FILE* file) : file_(file) {}

    FILE* OpenFile(const char* path) const override {
      if (strcmp(path, "/path/to/test.txt") == 0) {
        return file_;
      }
      errno = ENOENT;
      return nullptr;
    }

   private:
    FILE* const file_;
  };

  FakeFileProvider fake_file_provider(mem_file);

  FakeRadarDevice device;
  EXPECT_OK(device.RunRadarUtil({"radarutil", "-b", "20", "-o", "/path/to/test.txt"},
                                &fake_file_provider));
  EXPECT_GE(device.GetBurstsSent(), 20);
  ASSERT_NO_FATAL_FAILURE(device.Ok());

  // Each burst should have an increasing ID inserted by the fake radar device.
  size_t last_burst_id;
  memcpy(&last_burst_id, file_buffer.data(), sizeof(last_burst_id));
  EXPECT_NE(last_burst_id, 0);

  for (size_t i = 1; i < 20; i++) {
    size_t burst_id;
    memcpy(&burst_id, file_buffer.data() + (FakeRadarDevice::kBurstSize * i), sizeof(burst_id));
    EXPECT_EQ(burst_id, last_burst_id + 1);
    last_burst_id = burst_id;
  }
}

TEST(RadarUtilTest, InvalidArgs) {
  FakeRadarDevice device;
  EXPECT_NOT_OK(device.RunRadarUtil({"radarutil", "-t", "1s", "-v", "20", "-p", "999"}));
  EXPECT_NOT_OK(device.RunRadarUtil({"radarutil", "-t", "1s", "-v", "zzz", "-p", "1ms"}));
  EXPECT_NOT_OK(device.RunRadarUtil({"radarutil", "-t", "1s", "-v", "-3", "-p", "1ms"}));
  EXPECT_NOT_OK(device.RunRadarUtil({"radarutil", "-t", "1s", "-b", "20"}));
}

TEST(RadarUtilTest, InjectError) {
  FakeRadarDevice device;
  device.SetErrorOnBurst(10);

  EXPECT_EQ(device.RunRadarUtil({"radarutil", "-t", "10s", "-v", "20", "-p", "1ms"}), ZX_ERR_IO);
  EXPECT_EQ(device.GetRegisteredVmoCount(), 20);
  ASSERT_NO_FATAL_FAILURE(device.Ok());
}

TEST(RadarUtilTest, Help) {
  FakeRadarDevice device;
  EXPECT_OK(device.RunRadarUtil({"radarutil", "-h"}));
  EXPECT_EQ(device.GetRegisteredVmoCount(), 0);
}

}  // namespace radarutil
