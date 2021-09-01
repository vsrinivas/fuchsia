// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_DEVICE_H_

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/sync/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <cstdint>
#include <list>
#include <optional>
#include <unordered_set>

#include <ddktl/device.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/devices/lib/acpi/client.h"
#include "src/graphics/drivers/misc/goldfish_sync/sync_common_defs.h"

namespace goldfish {

namespace sync {

class SyncDevice;
using SyncDeviceType =
    ddk::Device<SyncDevice, ddk::Messageable<fuchsia_hardware_goldfish::SyncDevice>::Mixin>;

class SyncTimeline;

class SyncDevice : public SyncDeviceType,
                   public ddk::GoldfishSyncProtocol<SyncDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  SyncDevice(zx_device_t* parent, bool can_read_multiple_commands, acpi::Client client);
  ~SyncDevice();

  zx_status_t Bind();

  // Device protocol implementation.
  void DdkRelease();

  // |ddk.protocol.goldfish.sync|
  zx_status_t GoldfishSyncCreateTimeline(zx::channel request);

  // |fidl::WireServer<fuchsia_hardware_goldfish::Sync>|
  void CreateTimeline(CreateTimelineRequestView request,
                      CreateTimelineCompleter::Sync& completer) override;

  // Send guest->host command to sync device and notify the device.
  // Used only by |SyncTimeline|.
  void SendGuestCommand(GuestCommand command);

  // Shared async loop across all created Sync timelines. All incoming FIDL
  // calls and event waits will be posted on this loop.
  async::Loop* loop() { return &loop_; }

 protected:
  // Executes given "host->guest" command. Used only by |HandleStagedCommands()|
  // and test device classes.
  void RunHostCommand(HostCommand command);

 private:
  // Read all host->guest commands sent from sync device to the driver and
  // stage them into |staged_commands_|.
  // Returns true if |staged_commands_| transitions from empty to non-empty.
  bool ReadCommands();

  // Process all staged host commands.
  void HandleStagedCommands();

  // Send host->guest command results back to sync device.
  void ReplyHostCommand(HostCommand command);

  int IrqHandler();

  // Some devices may only read one host command fed to the device at a time.
  // These device should set this value to false to limit numbers of host
  // commands to read on each interrupt.
  const bool can_read_multiple_commands_;

  ddk::AcpiProtocolClient acpi_;
  acpi::Client acpi_fidl_;
  zx::interrupt irq_;
  zx::bti bti_;
  ddk::IoBuffer io_buffer_ TA_GUARDED(cmd_lock_);

  std::optional<thrd_t> irq_thread_{};

  // Holds active |SyncTimeline| instances. |SyncTimeline| instances are
  // both ref-counted by the device (for active channels) and fences it creates,
  // so we refer to them as RefPtrs here.
  fbl::DoublyLinkedList<fbl::RefPtr<SyncTimeline>> timelines_;

  std::list<HostCommand> staged_commands_;

  fbl::Mutex cmd_lock_ TA_ACQ_BEFORE(mmio_lock_);
  fbl::Mutex mmio_lock_ TA_ACQ_AFTER(cmd_lock_);
  std::optional<ddk::MmioBuffer> mmio_ TA_GUARDED(mmio_lock_);

  async::Loop loop_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(SyncDevice);
};

// |SyncTimeline| keeps a monotonously increasing timeline to manage
// all the sync fences it creates; The sync device can increase the timeline
// value on |SyncTimeline| and let it signal associated fence events so that
// clients can be notified.
//
// A Timeline can be depended by both user-space fence events (no matter whether
// they are still active) and clients which need to trigger host wait commands,
// so both |SyncDevice| and |Fence| hold a RefPtr to the |SyncTimeline| so that
// it won't be destroyed until the client breaks the FIDL channel and closed
// all the fence event handles.
class SyncTimeline : public fbl::RefCounted<SyncTimeline>,
                     public fbl::DoublyLinkedListable<fbl::RefPtr<SyncTimeline>,
                                                      fbl::NodeOptions::AllowRemoveFromContainer>,
                     public fidl::WireServer<fuchsia_hardware_goldfish::SyncTimeline> {
 public:
  explicit SyncTimeline(SyncDevice* parent);
  ~SyncTimeline();

  zx_status_t Bind(fidl::ServerEnd<fuchsia_hardware_goldfish::SyncTimeline> request);
  void OnClose(fidl::UnbindInfo info, zx::channel channel);

  // |fidl::WireServer<fuchsia_hardware_goldfish::SyncTimeline>|
  void TriggerHostWait(TriggerHostWaitRequestView request,
                       TriggerHostWaitCompleter::Sync& completer) override;

  // Create a new sync fence using given |event| and add it to the |fences_|
  // set.
  //
  // To handle fence lifetime, we also add an async wait to its parent loop
  // for |ZX_EVENTPAIR_PEER_CLOSED| signal on |event|. Once the couterpart of
  // |event| is closed, we'll destroy the Fence instance.
  void CreateFence(zx::eventpair event, std::optional<uint64_t> seqno = std::nullopt);

  // Increase the timeline sequence number, and update all active fences:
  // If an active fence should be signaled after the timeline increase,
  // signal the fence event and label it as "inactive".
  void Increase(uint64_t step);

 private:
  struct Fence : public fbl::DoublyLinkedListable<std::unique_ptr<Fence>,
                                                  fbl::NodeOptions::AllowRemoveFromContainer> {
    fbl::RefPtr<SyncTimeline> timeline;
    zx::eventpair event;
    uint64_t seqno = 0u;
    std::unique_ptr<async::Wait> peer_closed_wait;
  };

  SyncDevice* parent_device_;
  async_dispatcher_t* dispatcher_;

  fbl::Mutex lock_;
  uint64_t seqno_ TA_GUARDED(lock_) = 0;

  // Store all the fences created on this timeline:
  // - Active fences are sorted in increasing order of seqno.
  // - Active fences are signaled if current timeline seqno >= fence seqno.
  // - Fences (no matter whether active) will be removed and destroyed when
  //   the eventpair's peer event is closed (i.e. client closes the event).
  fbl::DoublyLinkedList<std::unique_ptr<Fence>> active_fences_ TA_GUARDED(lock_);
  fbl::DoublyLinkedList<std::unique_ptr<Fence>> inactive_fences_ TA_GUARDED(lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(SyncTimeline);
};

}  // namespace sync

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_DEVICE_H_
