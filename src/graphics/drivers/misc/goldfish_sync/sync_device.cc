// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_sync/sync_device.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/markers.h>
#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/threads.h>

#include <iterator>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>

#include "src/devices/lib/acpi/client.h"
#include "src/graphics/drivers/misc/goldfish_sync/goldfish_sync-bind.h"
#include "src/graphics/drivers/misc/goldfish_sync/sync_common_defs.h"

namespace goldfish {

namespace sync {

namespace {

uint32_t upper_32_bits(uint64_t n) { return static_cast<uint32_t>(n >> 32); }

uint32_t lower_32_bits(uint64_t n) { return static_cast<uint32_t>(n); }

}  // namespace

// static
zx_status_t SyncDevice::Create(void* ctx, zx_device_t* device) {
  auto client = acpi::Client::Create(device);
  if (client.is_error()) {
    return client.status_value();
  }

  async_dispatcher_t* dispatcher =
      fdf_dispatcher_get_async_dispatcher(fdf_dispatcher_get_current_dispatcher());
  auto sync_device = std::make_unique<goldfish::sync::SyncDevice>(
      device, /* can_read_multiple_commands= */ true, std::move(client.value()), dispatcher);

  zx_status_t status = sync_device->Bind();
  if (status == ZX_OK) {
    // devmgr now owns device.
    __UNUSED auto* dev = sync_device.release();
  }
  return status;
}

SyncDevice::SyncDevice(zx_device_t* parent, bool can_read_multiple_commands, acpi::Client client,
                       async_dispatcher_t* dispatcher)
    : SyncDeviceType(parent),
      can_read_multiple_commands_(can_read_multiple_commands),
      acpi_fidl_(std::move(client)),
      loop_(&kAsyncLoopConfigNeverAttachToThread),
      dispatcher_(dispatcher) {
  loop_.StartThread("goldfish-sync-loop-thread");
}

SyncDevice::~SyncDevice() {
  if (irq_.is_valid()) {
    irq_.destroy();
  }
  if (irq_thread_.has_value()) {
    thrd_join(irq_thread_.value(), nullptr);
  }
  loop_.Shutdown();
}

zx_status_t SyncDevice::Bind() {
  auto bti_result = acpi_fidl_.borrow()->GetBti(0);
  if (!bti_result.ok() || bti_result->is_error()) {
    zx_status_t status = bti_result.ok() ? bti_result->error_value() : bti_result.status();
    zxlogf(ERROR, "GetBti failed: %s", zx_status_get_string(status));
    return status;
  }
  bti_ = std::move(bti_result->value()->bti);

  auto mmio_result = acpi_fidl_.borrow()->GetMmio(0);
  if (!mmio_result.ok() || mmio_result->is_error()) {
    zx_status_t status = mmio_result.ok() ? mmio_result->error_value() : mmio_result.status();
    zxlogf(ERROR, "GetMmio failed: %s", zx_status_get_string(status));
    return status;
  }

  {
    fbl::AutoLock lock(&mmio_lock_);
    auto& mmio = mmio_result->value()->mmio;
    zx_status_t status = fdf::MmioBuffer::Create(mmio.offset, mmio.size, std::move(mmio.vmo),
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "mmiobuffer create failed: %s", zx_status_get_string(status));
      return status;
    }
  }

  auto result = acpi_fidl_.borrow()->MapInterrupt(0);
  if (!result.ok() || result->is_error()) {
    zxlogf(ERROR, "map_interrupt failed: %d",
           !result.ok() ? result.status() : result->error_value());
    return result.status();
  }
  irq_.reset(result->value()->irq.release());

  irq_thread_.emplace(thrd_t{});
  int rc = thrd_create_with_name(
      &irq_thread_.value(), [](void* arg) { return static_cast<SyncDevice*>(arg)->IrqHandler(); },
      this, "goldfish_sync_irq_thread");
  if (rc != thrd_success) {
    irq_.destroy();
    return thrd_status_to_zx_status(rc);
  }

  fbl::AutoLock cmd_lock(&cmd_lock_);
  fbl::AutoLock mmio_lock(&mmio_lock_);
  static_assert(sizeof(CommandBuffers) <= PAGE_SIZE, "cmds size");
  zx_status_t status = io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "io_buffer_init failed: %s", zx_status_get_string(status));
    return status;
  }

  // Register the buffer addresses with the device.
  // Device requires the lower 32 bits to be sent first for each address.
  zx_paddr_t pa_batch_hostcmd = io_buffer_.phys() + offsetof(CommandBuffers, batch_hostcmd);
  mmio_->Write32(lower_32_bits(pa_batch_hostcmd), SYNC_REG_BATCH_COMMAND_ADDR);
  mmio_->Write32(upper_32_bits(pa_batch_hostcmd), SYNC_REG_BATCH_COMMAND_ADDR_HIGH);

  ZX_DEBUG_ASSERT(lower_32_bits(pa_batch_hostcmd) == mmio_->Read32(SYNC_REG_BATCH_COMMAND_ADDR));
  ZX_DEBUG_ASSERT(upper_32_bits(pa_batch_hostcmd) ==
                  mmio_->Read32(SYNC_REG_BATCH_COMMAND_ADDR_HIGH));

  zx_paddr_t pa_batch_guestcmd = io_buffer_.phys() + offsetof(CommandBuffers, batch_guestcmd);
  mmio_->Write32(lower_32_bits(pa_batch_guestcmd), SYNC_REG_BATCH_GUESTCOMMAND_ADDR);
  mmio_->Write32(upper_32_bits(pa_batch_guestcmd), SYNC_REG_BATCH_GUESTCOMMAND_ADDR_HIGH);

  ZX_DEBUG_ASSERT(lower_32_bits(pa_batch_guestcmd) ==
                  mmio_->Read32(SYNC_REG_BATCH_GUESTCOMMAND_ADDR));
  ZX_DEBUG_ASSERT(upper_32_bits(pa_batch_guestcmd) ==
                  mmio_->Read32(SYNC_REG_BATCH_GUESTCOMMAND_ADDR_HIGH));

  mmio_->Write32(0, SYNC_REG_INIT);

  outgoing_.emplace(loop_.dispatcher());
  outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_goldfish::SyncDevice>,
      fbl::MakeRefCounted<fs::Service>(
          [device = this](fidl::ServerEnd<fuchsia_hardware_goldfish::SyncDevice> request) mutable {
            fidl::BindServer(device->dispatcher_, std::move(request), device);
            return ZX_OK;
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  status = outgoing_->Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to service the outgoing directory: %s", zx_status_get_string(status));
    return status;
  }

  std::array offers = {
      fidl::DiscoverableProtocolName<fuchsia_hardware_goldfish::SyncDevice>,
  };

  return DdkAdd(ddk::DeviceAddArgs("goldfish-sync")
                    .set_flags(DEVICE_ADD_MUST_ISOLATE)
                    .set_fidl_protocol_offers(offers)
                    .set_outgoing_dir(endpoints->client.TakeChannel())
                    .set_proto_id(ZX_PROTOCOL_GOLDFISH_SYNC));
}

void SyncDevice::DdkRelease() { delete this; }

zx_status_t SyncDevice::CreateTimeline(
    fidl::ServerEnd<fuchsia_hardware_goldfish::SyncTimeline> request) {
  fbl::RefPtr<SyncTimeline> timeline = fbl::MakeRefCounted<SyncTimeline>(this);
  timelines_.push_back(timeline);
  timeline->Bind(std::move(request));
  return ZX_OK;
}

void SyncDevice::CreateTimeline(CreateTimelineRequestView request,
                                CreateTimelineCompleter::Sync& completer) {
  CreateTimeline(std::move(request->timeline_req));
  completer.Reply();
}

bool SyncDevice::ReadCommands() {
  fbl::AutoLock cmd_lock(&cmd_lock_);
  fbl::AutoLock mmio_lock(&mmio_lock_);
  bool staged_commands_was_empty = staged_commands_.empty();
  while (true) {
    mmio_->Read32(SYNC_REG_BATCH_COMMAND);
    auto cmd_bufs = reinterpret_cast<CommandBuffers*>(io_buffer_.virt());
    if (cmd_bufs->batch_hostcmd.cmd == 0) {
      // no more new commands
      break;
    }

    staged_commands_.push_back(cmd_bufs->batch_hostcmd);
    if (!can_read_multiple_commands_) {
      break;
    }
  }
  return staged_commands_was_empty && !staged_commands_.empty();
}

void SyncDevice::RunHostCommand(HostCommand command) {
  switch (command.cmd) {
    case CMD_SYNC_READY: {
      TRACE_DURATION("gfx", "Sync::HostCommand::Ready");
      break;
    }
    case CMD_CREATE_SYNC_FENCE: {
      TRACE_DURATION("gfx", "Sync::HostCommand::CreateSyncFence", "timeline", command.handle,
                     "hostcmd_handle", command.hostcmd_handle);
      SyncTimeline* timeline = reinterpret_cast<SyncTimeline*>(command.handle);
      ZX_DEBUG_ASSERT(timeline);

      zx::eventpair event_device, event_client;
      zx_status_t status = zx::eventpair::create(0u, &event_device, &event_client);
      ZX_DEBUG_ASSERT(status == ZX_OK);

      timeline->CreateFence(std::move(event_device), command.time_arg);
      ReplyHostCommand({
          .handle = event_client.release(),
          .hostcmd_handle = command.hostcmd_handle,
          .cmd = command.cmd,
          .time_arg = 0,
      });
      break;
    }
    case CMD_CREATE_SYNC_TIMELINE: {
      TRACE_DURATION("gfx", "Sync::HostCommand::CreateTimeline", "hostcmd_handle",
                     command.hostcmd_handle);
      fbl::RefPtr<SyncTimeline> timeline = fbl::MakeRefCounted<SyncTimeline>(this);
      timelines_.push_back(timeline);
      ReplyHostCommand({
          .handle = reinterpret_cast<uint64_t>(timeline.get()),
          .hostcmd_handle = command.hostcmd_handle,
          .cmd = command.cmd,
          .time_arg = 0,
      });
      break;
    }
    case CMD_SYNC_TIMELINE_INC: {
      TRACE_DURATION("gfx", "Sync::HostCommand::TimelineInc", "timeline", command.handle,
                     "time_arg", command.time_arg);
      SyncTimeline* timeline = reinterpret_cast<SyncTimeline*>(command.handle);
      ZX_DEBUG_ASSERT(timeline);
      timeline->Increase(command.time_arg);
      break;
    }
    case CMD_DESTROY_SYNC_TIMELINE: {
      TRACE_DURATION("gfx", "Sync::HostCommand::DestroySyncTimeline", "timeline", command.handle);
      SyncTimeline* timeline = reinterpret_cast<SyncTimeline*>(command.handle);
      ZX_DEBUG_ASSERT(timeline);
      ZX_DEBUG_ASSERT(timeline->InContainer());
      timeline->RemoveFromContainer();
      break;
    }
  }
}

void SyncDevice::ReplyHostCommand(HostCommand command) {
  fbl::AutoLock cmd_lock(&cmd_lock_);
  auto cmd_bufs = reinterpret_cast<CommandBuffers*>(io_buffer_.virt());
  memcpy(&cmd_bufs->batch_hostcmd, &command, sizeof(HostCommand));

  fbl::AutoLock mmio_lock(&mmio_lock_);
  mmio_->Write32(0, SYNC_REG_BATCH_COMMAND);
}

void SyncDevice::SendGuestCommand(GuestCommand command) {
  fbl::AutoLock cmd_lock(&cmd_lock_);
  auto cmd_bufs = reinterpret_cast<CommandBuffers*>(io_buffer_.virt());
  memcpy(&cmd_bufs->batch_guestcmd, &command, sizeof(GuestCommand));

  fbl::AutoLock mmio_lock(&mmio_lock_);
  mmio_->Write32(0, SYNC_REG_BATCH_GUESTCOMMAND);
}

void SyncDevice::HandleStagedCommands() {
  std::list<HostCommand> commands;

  {
    fbl::AutoLock cmd_lock(&cmd_lock_);
    commands.splice(commands.begin(), staged_commands_, staged_commands_.begin(),
                    staged_commands_.end());
    ZX_DEBUG_ASSERT(staged_commands_.empty());
  }

  for (const auto& command : commands) {
    RunHostCommand(command);
  }
}

int SyncDevice::IrqHandler() {
  while (true) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      // ZX_ERR_CANCELED means the ACPI irq is cancelled, and the interrupt
      // thread should exit normally.
      if (status != ZX_ERR_CANCELED) {
        zxlogf(ERROR, "irq.wait() got %s", zx_status_get_string(status));
      }
      break;
    }

    // Handle incoming commands.
    if (ReadCommands()) {
      async::PostTask(loop_.dispatcher(), [this]() { HandleStagedCommands(); });
    }
  }

  return 0;
}

SyncTimeline::SyncTimeline(SyncDevice* parent)
    : parent_device_(parent), dispatcher_(parent->loop()->dispatcher()) {}

SyncTimeline::~SyncTimeline() = default;

zx_status_t SyncTimeline::Bind(fidl::ServerEnd<fuchsia_hardware_goldfish::SyncTimeline> request) {
  return async::PostTask(dispatcher_, [request = std::move(request), this]() mutable {
    using SyncTimelineProtocol = fuchsia_hardware_goldfish::SyncTimeline;
    fidl::BindServer(dispatcher_, std::move(request), this,
                     [](SyncTimeline* self, fidl::UnbindInfo info,
                        fidl::ServerEnd<SyncTimelineProtocol> server_end) {
                       self->OnClose(info, server_end.TakeChannel());
                     });
  });
  return ZX_OK;
}

void SyncTimeline::OnClose(fidl::UnbindInfo info, zx::channel channel) {
  if (!info.is_user_initiated()) {
    if (info.is_peer_closed()) {
      zxlogf(INFO, "client closed SyncTimeline connection");
    } else if (info.status() == ZX_ERR_CANCELED) {
      zxlogf(INFO, "dispatcher cancelled SyncTimeline");
    } else {
      zxlogf(ERROR, "channel internal error: %s", info.FormatDescription().c_str());
    }
  }

  if (InContainer()) {
    RemoveFromContainer();
  }
}

void SyncTimeline::TriggerHostWait(TriggerHostWaitRequestView request,
                                   TriggerHostWaitCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Sync::GuestCommand::TriggerHostWait", "timeline", this, "glsync",
                 request->host_glsync_handle, "syncthread", request->host_syncthread_handle);
  CreateFence(std::move(request->event));
  parent_device_->SendGuestCommand({.host_command = CMD_TRIGGER_HOST_WAIT,
                                    .glsync_handle = request->host_glsync_handle,
                                    .thread_handle = request->host_syncthread_handle,
                                    .guest_timeline_handle = reinterpret_cast<uint64_t>(this)});
}

void SyncTimeline::Increase(uint64_t step) {
  TRACE_DURATION("gfx", "SyncTimeline::Increase", "timeline", this, "step", step);
  fbl::AutoLock lock(&lock_);

  seqno_ += step;
  while (!active_fences_.is_empty()) {
    if (seqno_ < active_fences_.front().seqno) {
      break;
    }
    auto fence = active_fences_.pop_front();
    fence->event.signal_peer(0u, ZX_EVENTPAIR_SIGNALED);
    inactive_fences_.push_back(std::move(fence));
  }
}

void SyncTimeline::CreateFence(zx::eventpair event, std::optional<uint64_t> seqno) {
  TRACE_DURATION("gfx", "SyncTimeline::CreateFence", "timeline", this);

  std::unique_ptr<Fence> fence = std::make_unique<Fence>();
  Fence* fence_ptr = fence.get();
  {
    fbl::AutoLock lock(&lock_);

    fence->timeline = fbl::RefPtr(this);
    fence->event = std::move(event);
    fence->seqno = seqno.value_or(seqno_ + 1);
    // If the event's peer sent to the clients is closed, we can safely remove the
    // fence.
    fence->peer_closed_wait = std::make_unique<async::Wait>(
        fence_ptr->event.get(), ZX_EVENTPAIR_PEER_CLOSED, 0u,
        // We keep the RefPtr of |this| so that we can ensure |timeline| is
        // always valid in the callback, otherwise when the last fence is
        // removed from the container, it will destroy the sync timeline and
        // cause a use-after-free error.
        [fence = fence_ptr, timeline = fbl::RefPtr(this)](async_dispatcher_t* dispatcher,
                                                          async::Wait* wait, zx_status_t status,
                                                          const zx_packet_signal_t* signal) {
          if (signal == nullptr || (signal->observed & ZX_EVENTPAIR_PEER_CLOSED)) {
            if (status != ZX_OK && status != ZX_ERR_CANCELED) {
              zxlogf(ERROR, "CreateFence: Unexpected Wait status: %s",
                     zx_status_get_string(status));
            }
            // Since |fence| holds the async Wait (and its lambda captures),
            // when |fence| is deleted, there will be no references to
            // |timeline| and the mutex below will be deleted. We need to hold
            // |fence_to_delete| to make sure that it's deleted later than
            // |timeline|.
            std::unique_ptr<Fence> fence_to_delete;
            {
              fbl::AutoLock lock(&timeline->lock_);
              ZX_DEBUG_ASSERT(fence->InContainer());
              fence_to_delete = fence->RemoveFromContainer();
            }
          }
        });

    if (seqno_ >= fence->seqno) {
      // Fence is inactive. Store it in |inactive_fences_| list until its
      // peer disconnects.
      inactive_fences_.push_back(std::move(fence));
    } else {
      // Maintain the seqno order in the active fences linked list.
      auto iter = active_fences_.end();
      while (iter != active_fences_.begin()) {
        if ((--iter)->seqno < fence->seqno) {
          ++iter;
          break;
        }
      }
      active_fences_.insert(iter, std::move(fence));
    }
  }

  fence_ptr->peer_closed_wait->Begin(dispatcher_);
}

}  // namespace sync

}  // namespace goldfish

static constexpr zx_driver_ops_t goldfish_sync_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::sync::SyncDevice::Create;
  return ops;
}();

ZIRCON_DRIVER(goldfish_sync, goldfish_sync_driver_ops, "zircon", "0.1");
