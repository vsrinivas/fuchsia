// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_DEVICE_H_

#include <fidl/fuchsia.hardware.goldfish.pipe/cpp/wire.h>
#include <fidl/fuchsia.hardware.sysmem/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/svc/outgoing.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/event.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "src/devices/lib/acpi/client.h"

namespace goldfish {

class PipeDevice;
using DeviceType = ddk::Device<PipeDevice>;

// |PipeDevice| is the "root" ACPI device that creates pipes and executes pipe
// operations. It could create multiple |PipeChildDevice| instances using
// |CreateChildDevice| method, each having its own properties so that they can
// be bound to different drivers, but sharing the same parent |PipeDevice|.
class PipeDevice : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit PipeDevice(zx_device_t* parent, acpi::Client client);
  ~PipeDevice();

  zx_status_t Bind();
  zx_status_t CreateChildDevice(cpp20::span<const zx_device_prop_t> props, const char* dev_name);

  // Device protocol implementation.
  void DdkRelease();
  zx_status_t Create(int32_t* out_id, zx::vmo* out_vmo);
  zx_status_t SetEvent(int32_t id, zx::event pipe_event);
  void Destroy(int32_t id);
  void Open(int32_t id);
  void Exec(int32_t id);
  zx_status_t GetBti(zx::bti* out_bti);
  zx_status_t ConnectSysmem(zx::channel connection);
  zx_status_t RegisterSysmemHeap(uint64_t heap, zx::channel connection);

  int IrqHandler();

 private:
  struct Pipe {
    Pipe(zx_paddr_t paddr, zx::pmt pmt, zx::event pipe_event);
    ~Pipe();

    void SignalEvent(uint32_t flags) const;

    const zx_paddr_t paddr;
    zx::pmt pmt;
    zx::event pipe_event;
  };

  fidl::WireSyncClient<fuchsia_hardware_sysmem::Sysmem> sysmem_;
  acpi::Client acpi_fidl_;
  zx::interrupt irq_;
  zx::bti bti_;
  ddk::IoBuffer io_buffer_;
  thrd_t irq_thread_{};
  int32_t next_pipe_id_ TA_GUARDED(pipes_lock_) = 1;

  fbl::Mutex mmio_lock_;
  std::optional<fdf::MmioBuffer> mmio_ TA_GUARDED(mmio_lock_);

  fbl::Mutex pipes_lock_;
  // TODO(fxbug.dev/3213): This should be std::unordered_map.
  using PipeMap = std::map<int32_t, std::unique_ptr<Pipe>>;
  PipeMap pipes_ TA_GUARDED(pipes_lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(PipeDevice);
};

class PipeChildDevice;
using PipeChildDeviceType = ddk::Device<PipeChildDevice, ddk::Unbindable, ddk::Openable>;

// |PipeChildDevice| is created by |PipeDevice| and serves the
// |fuchsia.hardware.goldfish.GoldfishPipe| FIDL protocol by forwarding all the
// FIDL requests to the parent device.
class PipeChildDevice : public PipeChildDeviceType,
                        public fidl::WireServer<fuchsia_hardware_goldfish_pipe::GoldfishPipe> {
 public:
  explicit PipeChildDevice(PipeDevice* parent, async_dispatcher_t* dispatcher);
  ~PipeChildDevice() = default;

  zx_status_t Bind(cpp20::span<const zx_device_prop_t> props, const char* dev_name);

  // Device protocol implementation.
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override;
  void SetEvent(SetEventRequestView request, SetEventCompleter::Sync& completer) override;
  void Destroy(DestroyRequestView request, DestroyCompleter::Sync& completer) override;
  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override;
  void Exec(ExecRequestView request, ExecCompleter::Sync& completer) override;
  void GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) override;
  void ConnectSysmem(ConnectSysmemRequestView request,
                     ConnectSysmemCompleter::Sync& completer) override;
  void RegisterSysmemHeap(RegisterSysmemHeapRequestView request,
                          RegisterSysmemHeapCompleter::Sync& completer) override;

 private:
  PipeDevice* parent_ = nullptr;
  std::optional<svc::Outgoing> outgoing_;
  async_dispatcher_t* dispatcher_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_DEVICE_H_
