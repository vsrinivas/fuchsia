// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/promise.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <atomic>

#include <fbl/auto_lock.h>

#include "src/devices/board/drivers/x86/acpi/event.h"
#include "src/devices/board/drivers/x86/acpi/fidl.h"
#include "src/devices/board/drivers/x86/acpi/global-lock.h"
#include "src/devices/board/drivers/x86/acpi/manager.h"
#include "src/devices/board/drivers/x86/include/errors.h"
#include "src/devices/board/drivers/x86/include/sysmem.h"
#include "src/devices/lib/iommu/iommu.h"

namespace acpi {
namespace {
// Maximum number of pending Device Object Notifications before we stop sending them to a device.
constexpr size_t kMaxPendingNotifications = 1000;
}  // namespace

const char* BusTypeToString(BusType t) {
  switch (t) {
    case kPci:
      return "pci";
    case kSpi:
      return "spi";
    case kI2c:
      return "i2c";
    case kUnknown:
      return "unknown";
  }
}

ACPI_STATUS Device::AddResource(ACPI_RESOURCE* res) {
  if (resource_is_memory(res)) {
    resource_memory_t mem;
    zx_status_t st = resource_parse_memory(res, &mem);
    // only expect fixed memory resource. resource_parse_memory sets minimum == maximum
    // for this memory resource type.
    if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
      return AE_ERROR;
    }
    mmio_resources_.emplace_back(mem);

  } else if (resource_is_address(res)) {
    resource_address_t addr;
    zx_status_t st = resource_parse_address(res, &addr);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
        addr.max_address_fixed && (addr.maximum < addr.minimum)) {
      mmio_resources_.emplace_back(/* writeable= */ true, addr.min_address_fixed,
                                   /* alignment= */ 0, static_cast<uint32_t>(addr.address_length));
    }

  } else if (resource_is_io(res)) {
    resource_io_t io;
    zx_status_t st = resource_parse_io(res, &io);
    if (st != ZX_OK) {
      return AE_ERROR;
    }

    pio_resources_.emplace_back(io);

  } else if (resource_is_irq(res)) {
    resource_irq_t irq;
    zx_status_t st = resource_parse_irq(res, &irq);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    for (auto i = 0; i < irq.pin_count; i++) {
      irqs_.emplace_back(irq, i);
    }
  }

  return AE_OK;
}

zx_status_t Device::ReportCurrentResources() {
  if (got_resources_) {
    return ZX_OK;
  }

  // Check device state.
  auto state = acpi_->EvaluateObject(acpi_handle_, "_STA", std::nullopt);
  uint64_t sta;
  if (state.is_error() || state->Type != ACPI_TYPE_INTEGER) {
    sta = 0xf;
  } else {
    sta = state->Integer.Value;
  }

  if ((sta & ACPI_STA_DEVICE_ENABLED) == 0) {
    // We're not allowed to enumerate resources if the device is not enabled.
    // see ACPI 6.4 section 6.3.7.
    return ZX_OK;
  }

  // call _CRS to fill in resources
  ACPI_STATUS acpi_status = AcpiWalkResources(
      acpi_handle_, (char*)"_CRS",
      [](ACPI_RESOURCE* res, void* ctx) __TA_REQUIRES(reinterpret_cast<Device*>(ctx)->lock_) {
        return reinterpret_cast<Device*>(ctx)->AddResource(res);
      },
      this);
  if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
    return acpi_to_zx_status(acpi_status);
  }

  zxlogf(DEBUG, "acpi-bus: found %zd port resources %zd memory resources %zx irqs",
         pio_resources_.size(), mmio_resources_.size(), irqs_.size());
  if (zxlog_level_enabled(TRACE)) {
    zxlogf(TRACE, "port resources:");
    for (size_t i = 0; i < pio_resources_.size(); i++) {
      zxlogf(TRACE, "  %02zd: addr=0x%x length=0x%x align=0x%x", i, pio_resources_[i].base_address,
             pio_resources_[i].address_length, pio_resources_[i].alignment);
    }
    zxlogf(TRACE, "memory resources:");
    for (size_t i = 0; i < mmio_resources_.size(); i++) {
      zxlogf(TRACE, "  %02zd: addr=0x%x length=0x%x align=0x%x writeable=%d", i,
             mmio_resources_[i].base_address, mmio_resources_[i].address_length,
             mmio_resources_[i].alignment, mmio_resources_[i].writeable);
    }
    zxlogf(TRACE, "irqs:");
    for (size_t i = 0; i < irqs_.size(); i++) {
      const char* trigger;
      switch (irqs_[i].trigger) {
        case ACPI_IRQ_TRIGGER_EDGE:
          trigger = "edge";
          break;
        case ACPI_IRQ_TRIGGER_LEVEL:
          trigger = "level";
          break;
        default:
          trigger = "bad_trigger";
          break;
      }
      const char* polarity;
      switch (irqs_[i].polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          polarity = "both";
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          polarity = "low";
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          polarity = "high";
          break;
        default:
          polarity = "bad_polarity";
          break;
      }
      zxlogf(TRACE, "  %02zd: pin=%u %s %s %s %s", i, irqs_[i].pin, trigger, polarity,
             (irqs_[i].sharable == ACPI_IRQ_SHARED) ? "shared" : "exclusive",
             irqs_[i].wake_capable ? "wake" : "nowake");
    }
  }

  got_resources_ = true;

  return ZX_OK;
}

void Device::DdkInit(ddk::InitTxn txn) {
  auto use_global_lock = acpi_->EvaluateObject(acpi_handle_, "_GLK", std::nullopt);
  if (use_global_lock.is_ok()) {
    if (use_global_lock->Type == ACPI_TYPE_INTEGER && use_global_lock->Integer.Value == 1) {
      can_use_global_lock_ = true;
    }
  }

  if (metadata_.empty()) {
    txn.Reply(ZX_OK);
    return;
  }
  zx_status_t result = ZX_OK;
  switch (bus_type_) {
    case BusType::kSpi:
      result = DdkAddMetadata(DEVICE_METADATA_SPI_CHANNELS, metadata_.data(), metadata_.size());
      break;
    case BusType::kI2c:
      result = DdkAddMetadata(DEVICE_METADATA_I2C_CHANNELS, metadata_.data(), metadata_.size());
      break;
    default:
      break;
  }

  txn.Reply(result);
}

void Device::DdkUnbind(ddk::UnbindTxn txn) {
  if (notify_handler_.has_value()) {
    RemoveNotifyHandler();
  }

  std::optional<fpromise::promise<void>> address_handler_finished;
  {
    std::scoped_lock lock(address_handler_lock_);
    for (auto& entry : address_handlers_) {
      entry.second.AsyncTeardown();
    }

    address_handler_finished.emplace(
        fpromise::join_promise_vector(std::move(address_handler_teardown_finished_))
            .discard_result());
  }

  std::optional<fpromise::promise<void>> teardown_finished;
  notify_teardown_finished_.swap(teardown_finished);
  auto promise = fpromise::join_promises(
                     std::move(teardown_finished).value_or(fpromise::make_ok_promise()),
                     std::move(address_handler_finished).value_or(fpromise::make_ok_promise()))
                     .discard_result()
                     .and_then([txn = std::move(txn)]() mutable { txn.Reply(); });
  manager_->executor().schedule_task(std::move(promise));
}

void Device::GetMmio(GetMmioRequestView request, GetMmioCompleter::Sync& completer) {
  std::scoped_lock guard{lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }

  if (request->index >= mmio_resources_.size()) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  const DeviceMmioResource& res = mmio_resources_[request->index];
  // TODO(fxbug.dev/67899): This check becomes overly pessimistic at larger page sizes.
  if (((res.base_address & (zx_system_get_page_size() - 1)) != 0) ||
      ((res.address_length & (zx_system_get_page_size() - 1)) != 0)) {
    zxlogf(ERROR, "acpi-bus: memory id=%d addr=0x%08x len=0x%x is not page aligned", request->index,
           res.base_address, res.address_length);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  zx_handle_t vmo;
  size_t size{res.address_length};
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  st = zx_vmo_create_physical(get_root_resource(), res.base_address, size, &vmo);
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }

  completer.ReplySuccess(fuchsia_mem::wire::Range{
      .vmo = zx::vmo(vmo),
      .offset = 0,
      .size = size,
  });
}

void Device::GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) {
  // We only support getting BTIs for devices with no bus.
  if (bus_type_ != BusType::kUnknown) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (request->index != 0) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  // For dummy IOMMUs, the bti_id just needs to be unique.
  // We assume that the device will never get an actual BTI
  // because it is a pure ACPI device.
  //
  // TODO(fxbug.dev/92140): check the DMAR for ACPI entries.
  zx_handle_t iommu_handle;
  zx_status_t status = iommu_manager_dummy_iommu(&iommu_handle);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  }
  zx::bti bti;
  zx::bti::create(*zx::unowned_iommu{iommu_handle}, 0, bti_id_, &bti);

  completer.ReplySuccess(std::move(bti));
}

void Device::AcpiConnectServer(zx::channel server) {
  zx_status_t status = ZX_OK;
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start FIDL thread: %s", zx_status_get_string(status));
    return;
  }

  status = fidl::BindSingleInFlightOnly(
      manager_->fidl_dispatcher(),
      fidl::ServerEnd<fuchsia_hardware_acpi::Device>(std::move(server)), this);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to bind channel: %s", zx_status_get_string(status));
  }
}

zx::status<zx::channel> Device::PrepareOutgoing() {
  outgoing_.emplace(manager_->fidl_dispatcher());
  outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_acpi::Device>,
      fbl::MakeRefCounted<fs::Service>(
          [this](fidl::ServerEnd<fuchsia_hardware_acpi::Device> request) mutable {
            return fidl::BindSingleInFlightOnly(manager_->fidl_dispatcher(), std::move(request),
                                                this);
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  auto st = outgoing_->Serve(std::move(endpoints->server));
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to serve the outgoing directory: %s", zx_status_get_string(st));
    return zx::error(st);
  }

  return zx::ok(endpoints->client.TakeChannel());
}

zx::status<> Device::AddDevice(const char* name, cpp20::span<zx_device_prop_t> props,
                               cpp20::span<zx_device_str_prop_t> str_props, uint32_t flags) {
  std::array offers = {
      fidl::DiscoverableProtocolName<fuchsia_hardware_acpi::Device>,
  };

  auto outgoing = PrepareOutgoing();
  if (outgoing.is_error()) {
    zxlogf(ERROR, "failed to add acpi device '%s' - while setting up outgoing: %s", name,
           outgoing.status_string());
    return outgoing.take_error();
  }

  return zx::make_status(DdkAdd(ddk::DeviceAddArgs(name)
                                    .set_props(props)
                                    .set_str_props(str_props)
                                    .set_proto_id(ZX_PROTOCOL_ACPI)
                                    .set_flags(flags | DEVICE_ADD_MUST_ISOLATE)
                                    .set_fidl_protocol_offers(offers)
                                    .set_outgoing_dir(std::move(outgoing.value()))));
}

void Device::GetBusId(GetBusIdRequestView request, GetBusIdCompleter::Sync& completer) {
  if (bus_id_ == UINT32_MAX) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
  } else {
    completer.ReplySuccess(bus_id_);
  }
}

void Device::EvaluateObject(EvaluateObjectRequestView request,
                            EvaluateObjectCompleter::Sync& completer) {
  auto helper = EvaluateObjectFidlHelper::FromRequest(acpi_, acpi_handle_, request);
  fidl::Arena<> alloc;
  auto result = helper.Evaluate(alloc);
  if (result.is_error()) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status(result.error_value()));
  } else {
    completer.Reply(std::move(result.value()));
  }
}

void Device::MapInterrupt(MapInterruptRequestView request, MapInterruptCompleter::Sync& completer) {
  std::scoped_lock guard{lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }

  uint64_t which_irq = request->index;
  if (which_irq >= irqs_.size()) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  const DeviceIrqResource& irq = irqs_[which_irq];
  uint32_t mode;
  mode = ZX_INTERRUPT_MODE_DEFAULT;
  st = ZX_OK;
  switch (irq.trigger) {
    case ACPI_IRQ_TRIGGER_EDGE:
      switch (irq.polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          mode = ZX_INTERRUPT_MODE_EDGE_BOTH;
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_EDGE_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_EDGE_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    case ACPI_IRQ_TRIGGER_LEVEL:
      switch (irq.polarity) {
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_LEVEL_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_LEVEL_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    default:
      st = ZX_ERR_INVALID_ARGS;
      break;
  }
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::interrupt out_irq;
  st = zx::interrupt::create(*zx::unowned_resource{get_root_resource()}, irq.pin,
                             ZX_INTERRUPT_REMAP_IRQ | mode, &out_irq);
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }

  completer.ReplySuccess(std::move(out_irq));
}

void Device::GetPio(GetPioRequestView request, GetPioCompleter::Sync& completer) {
  std::scoped_lock guard{lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    completer.ReplyError(st);
    return;
  }

  if (request->index >= pio_resources_.size()) {
    completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  const DevicePioResource& res = pio_resources_[request->index];

  char name[ZX_MAX_NAME_LEN];
  snprintf(name, ZX_MAX_NAME_LEN, "ioport-%u", request->index);

  zx::resource out_pio;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status =
      zx::resource::create(*zx::unowned_resource{get_root_resource()}, ZX_RSRC_KIND_IOPORT,
                           res.base_address, res.address_length, name, 0, &out_pio);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(std::move(out_pio));
  }
}

void Device::InstallNotifyHandler(InstallNotifyHandlerRequestView request,
                                  InstallNotifyHandlerCompleter::Sync& completer) {
  // Try and take the notification handler.
  // Will set is_active to true if is_active is already true.
  bool is_active = false;
  notify_handler_active_.compare_exchange_strong(is_active, true, std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
  if (notify_handler_ && notify_handler_->is_valid() && is_active) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kAlreadyExists);
    return;
  }
  notify_handler_type_ = uint32_t(request->mode);

  if (!request->handler.is_valid()) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kBadParameter);
    return;
  }

  if (request->mode.has_unknown_bits()) {
    zxlogf(WARNING, "Unknown mode bits for notify handler ignored: 0x%x",
           uint32_t(request->mode.unknown_bits()));
  }

  uint32_t mode(request->mode & fuchsia_hardware_acpi::wire::NotificationMode::kMask);

  auto async_completer = completer.ToAsync();
  std::optional<fpromise::promise<void>> teardown_finished;
  notify_teardown_finished_.swap(teardown_finished);
  auto promise =
      std::move(teardown_finished)
          .value_or(fpromise::make_ok_promise())
          .and_then([this, mode, async_completer = std::move(async_completer),
                     handler = std::move(request->handler)]() mutable {
            pending_notify_count_.store(0, std::memory_order_release);
            // Reset the "teardown finished" promise.
            fpromise::bridge<void> bridge;
            notify_teardown_finished_ = bridge.consumer.promise();
            auto notify_event_handler =
                std::make_unique<NotifyEventHandler>(this, std::move(bridge.completer));

            fidl::WireSharedClient<fuchsia_hardware_acpi::NotifyHandler> client(
                std::move(handler), manager_->fidl_dispatcher(), std::move(notify_event_handler));
            notify_handler_ = std::move(client);
            auto status = acpi_->InstallNotifyHandler(
                acpi_handle_, mode, Device::DeviceObjectNotificationHandler, this);
            if (status.is_error()) {
              notify_handler_.reset();
              async_completer.ReplyError(fuchsia_hardware_acpi::wire::Status(status.error_value()));
              return;
            }

            async_completer.ReplySuccess();
          })
          .box();
  manager_->executor().schedule_task(std::move(promise));
}

void Device::DeviceObjectNotificationHandler(ACPI_HANDLE object, uint32_t value, void* context) {
  Device* device = static_cast<Device*>(context);
  if (device->pending_notify_count_.load(std::memory_order_acquire) >= kMaxPendingNotifications) {
    if (!device->notify_count_warned_) {
      zxlogf(ERROR, "%s: too many un-handled pending notifications. Will drop notifications.",
             device->name());
      device->notify_count_warned_ = true;
    }
    return;
  }

  device->pending_notify_count_.fetch_add(1, std::memory_order_acq_rel);
  if (device->notify_handler_ && device->notify_handler_->is_valid()) {
    device->notify_handler_.value()->Handle(value, [device]() {
      device->pending_notify_count_.fetch_sub(1, std::memory_order_acq_rel);
    });
  }
}

void Device::RemoveNotifyHandler() {
  // Try and mark the notify handler as inactive. If this fails, then someone else marked it as
  // inactive.
  // If this succeeds, then we're going to tear down the notify handler.
  bool is_active = true;
  notify_handler_active_.compare_exchange_strong(is_active, false, std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
  if (!is_active) {
    return;
  }
  auto status = acpi_->RemoveNotifyHandler(acpi_handle_, notify_handler_type_,
                                           Device::DeviceObjectNotificationHandler);
  if (status.is_error()) {
    zxlogf(ERROR, "Failed to remove notification handler from '%s': %d", name(),
           status.error_value());
    return;
  }
  notify_handler_->AsyncTeardown();
}

void Device::AcquireGlobalLock(AcquireGlobalLockRequestView request,
                               AcquireGlobalLockCompleter::Sync& completer) {
  if (!can_use_global_lock_) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kAccess);
    return;
  }

  GlobalLockHandle::Create(acpi_, manager_->fidl_dispatcher(), completer.ToAsync());
}

ACPI_STATUS Device::AddressSpaceHandler(uint32_t function, ACPI_PHYSICAL_ADDRESS physical_address,
                                        uint32_t bit_width, UINT64* value, void* handler_ctx,
                                        void* region_ctx) {
  HandlerCtx* ctx = static_cast<HandlerCtx*>(handler_ctx);
  std::scoped_lock lock(ctx->device->address_handler_lock_);
  auto client = ctx->device->address_handlers_.find(ctx->space_type);
  if (client == ctx->device->address_handlers_.end()) {
    zxlogf(ERROR, "No handler found for space %u", ctx->space_type);
  }

  switch (function) {
    case ACPI_READ: {
      auto result = client->second.sync()->Read(physical_address, bit_width);
      if (!result.ok()) {
        zxlogf(ERROR, "FIDL Read failed: %s", result.FormatDescription().data());
        return AE_ERROR;
      }
      if (result->result.is_err()) {
        return result->result.err();
      }
      *value = result->result.response().value;
      break;
    }
    case ACPI_WRITE: {
      auto result = client->second.sync()->Write(physical_address, bit_width, *value);
      if (!result.ok()) {
        zxlogf(ERROR, "FIDL Write failed: %s", result.FormatDescription().data());
        return AE_ERROR;
      }
      if (result->result.is_err()) {
        return result->result.err();
      }
      break;
    }
  }
  return AE_OK;
}

void Device::InstallAddressSpaceHandler(InstallAddressSpaceHandlerRequestView request,
                                        InstallAddressSpaceHandlerCompleter::Sync& completer) {
  if (request->space.IsUnknown()) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kNotSupported);
    return;
  }

  std::scoped_lock lock(address_handler_lock_);
  uint32_t space(request->space);
  if (address_handlers_.find(space) != address_handlers_.end()) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status::kAlreadyExists);
    return;
  }

  // Allocated using new, and then destroyed by the FIDL teardown handler.
  auto ctx = std::make_unique<HandlerCtx>();
  ctx->device = this;
  ctx->space_type = space;

  // It's safe to do this now, because any address space requests will try and acquire the
  // address_handler_lock_. As a result, nothing will happen until we've finished setting up the
  // FIDL client and our bookkeeping below.
  auto status = acpi_->InstallAddressSpaceHandler(acpi_handle_, static_cast<uint8_t>(space),
                                                  AddressSpaceHandler, nullptr, ctx.get());
  if (status.is_error()) {
    completer.ReplyError(fuchsia_hardware_acpi::wire::Status(status.error_value()));
    return;
  }

  fpromise::bridge<void> bridge;
  fidl::WireSharedClient<fuchsia_hardware_acpi::AddressSpaceHandler> client(
      std::move(request->handler), manager_->fidl_dispatcher(),
      fidl::AnyTeardownObserver::ByCallback(
          [this, ctx = std::move(ctx), space, completer = std::move(bridge.completer)]() mutable {
            std::scoped_lock lock(address_handler_lock_);
            // Remove the address space handler from ACPICA.
            auto result = acpi_->RemoveAddressSpaceHandler(
                acpi_handle_, static_cast<uint8_t>(space), AddressSpaceHandler);
            if (result.is_error()) {
              zxlogf(ERROR, "Failed to remove address space handler: %d", result.status_value());
              // We're in a strange state now. Claim that we've torn down, but avoid freeing
              // things to minimise the chance of a UAF in the address space handler.
              ZX_DEBUG_ASSERT_MSG(false, "Failed to remove address space handler: %d",
                                  result.status_value());
              completer.complete_ok();
              return;
            }
            // Clean up other things.
            address_handlers_.erase(space);
            completer.complete_ok();
          }));

  // Everything worked, so insert our book-keeping.
  address_handler_teardown_finished_.emplace_back(bridge.consumer.promise());
  address_handlers_.emplace(space, std::move(client));

  completer.ReplySuccess();
}
}  // namespace acpi
