// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/platform/platform-bus.h"

#include <assert.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/markers.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver2/handlers.h>
#include <lib/fdf/dispatcher.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/driver-config.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls/iommu.h>

#include <algorithm>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include "src/devices/bus/drivers/platform/cpu-trace.h"
#include "src/devices/bus/drivers/platform/node-util.h"
#include "src/devices/bus/drivers/platform/platform-bus-bind.h"

namespace {
// Adds a passthrough device which forwards all banjo connections to the parent device.
// The device will be added as a child of |parent| with the name |name|, and |props| will
// be applied to the new device's add_args.
// Returns ZX_OK if the device is successfully added.
zx_status_t AddProtocolPassthrough(const char* name, cpp20::span<const zx_device_prop_t> props,
                                   platform_bus::PlatformBus* parent, zx_device_t** out_device) {
  if (!parent || !name) {
    return ZX_ERR_INVALID_ARGS;
  }

  static zx_protocol_device_t passthrough_proto = {
      .version = DEVICE_OPS_VERSION,
      .get_protocol =
          [](void* ctx, uint32_t id, void* proto) {
            return device_get_protocol(reinterpret_cast<platform_bus::PlatformBus*>(ctx)->zxdev(),
                                       id, proto);
          },
      .release = [](void* ctx) {},
  };

  driver::ServiceInstanceHandler handler;
  fuchsia_hardware_platform_bus::Service::Handler service(&handler);

  auto protocol = [parent](fdf::ServerEnd<fuchsia_hardware_platform_bus::PlatformBus> server_end) {
    fdf::BindServer<fdf::WireServer<fuchsia_hardware_platform_bus::PlatformBus>>(
        fdf::Dispatcher::GetCurrent()->get(), std::move(server_end), parent);
  };

  auto status = service.add_platform_bus(std::move(protocol));
  if (status.is_error()) {
    return status.error_value();
  }

  status =
      parent->outgoing().AddService<fuchsia_hardware_platform_bus::Service>(std::move(handler));
  if (status.is_error()) {
    return status.error_value();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto result = parent->outgoing().Serve(std::move(endpoints->server));
  if (result.is_error()) {
    return result.error_value();
  }

  std::array offers = {
      fuchsia_hardware_platform_bus::Service::Name,
  };

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = parent,
      .ops = &passthrough_proto,
      .props = props.data(),
      .prop_count = static_cast<uint32_t>(props.size()),
      .runtime_service_offers = offers.data(),
      .runtime_service_offer_count = offers.size(),
      .outgoing_dir_channel = endpoints->client.TakeChannel().release(),
  };

  return device_add(parent->zxdev(), &args, out_device);
}

}  // anonymous namespace

namespace platform_bus {

zx_status_t PlatformBus::IommuGetBti(uint32_t iommu_index, uint32_t bti_id, zx::bti* out_bti) {
  if (iommu_index != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  std::pair key(iommu_index, bti_id);
  auto bti = cached_btis_.find(key);
  if (bti == cached_btis_.end()) {
    zx::bti new_bti;
    zx_status_t status = zx::bti::create(iommu_handle_, 0, bti_id, &new_bti);
    if (status != ZX_OK) {
      return status;
    }
    auto [iter, _] = cached_btis_.emplace(key, std::move(new_bti));
    bti = iter;
  }

  return bti->second.duplicate(ZX_RIGHT_SAME_RIGHTS, out_bti);
}

void PlatformBus::RegisterProtocol(RegisterProtocolRequestView request, fdf::Arena& arena,
                                   RegisterProtocolCompleter::Sync& completer) {
  if (request->protocol.count() < sizeof(ddk::AnyProtocol)) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  const uint8_t* protocol = request->protocol.data();
  switch (request->proto_id) {
      // DO NOT ADD ANY MORE PROTOCOLS HERE.
      // GPIO_IMPL is needed for board driver pinmuxing. IOMMU is for potential future use.
      // CLOCK_IMPL are needed by the amlogic board drivers. Use of this mechanism for all other
      // protocols has been deprecated.
    case ZX_PROTOCOL_CLOCK_IMPL: {
      clock_ =
          ddk::ClockImplProtocolClient(reinterpret_cast<const clock_impl_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_GPIO_IMPL: {
      gpio_ = ddk::GpioImplProtocolClient(reinterpret_cast<const gpio_impl_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_IOMMU: {
      iommu_ = ddk::IommuProtocolClient(reinterpret_cast<const iommu_protocol_t*>(protocol));
      break;
    }
    default:
      completer.buffer(arena).ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  fbl::AutoLock lock(&proto_completion_mutex_);
  auto entry = proto_ready_responders_.find(request->proto_id);
  if (entry != proto_ready_responders_.end()) {
    auto& responder = entry->second;
    zx_status_t status = responder.timeout_task->Cancel();
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to cancel task: %s. Trying to respond anyway.",
             zx_status_get_string(status));
    }
    if (responder.completer.is_reply_needed()) {
      responder.completer.buffer(responder.arena).ReplySuccess();
    } else {
      zxlogf(ERROR, "Failed to register proto id 0x%x. It probably took too long.",
             request->proto_id);
    }
    proto_ready_responders_.erase(entry);
  }
  completer.buffer(arena).ReplySuccess();
}

void PlatformBus::NodeAdd(NodeAddRequestView request, fdf::Arena& arena,
                          NodeAddCompleter::Sync& completer) {
  if (!request->node.has_name()) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto natural = fidl::ToNatural(request->node);
  completer.buffer(arena).Reply(NodeAddInternal(natural));
}

zx::status<> PlatformBus::NodeAddInternal(fuchsia_hardware_platform_bus::Node& node) {
  auto result = ValidateResources(node);
  if (result.is_error()) {
    return result.take_error();
  }
  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto status =
      PlatformDevice::Create(std::move(node), zxdev(), this, PlatformDevice::Isolated, &dev);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = dev->Start();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return zx::ok();
}

void PlatformBus::ProtocolNodeAdd(ProtocolNodeAddRequestView request, fdf::Arena& arena,
                                  ProtocolNodeAddCompleter::Sync& completer) {
  if (!request->node.has_name()) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto natural = fidl::ToNatural(request->node);
  auto result = ValidateResources(natural);
  if (result.is_error()) {
    completer.buffer(arena).ReplyError(result.status_value());
    return;
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto status =
      PlatformDevice::Create(std::move(natural), zxdev(), this, PlatformDevice::Protocol, &dev);
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }

  status = dev->Start();
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  // Wait for protocol implementation driver to register its protocol.
  fbl::AutoLock<fbl::Mutex> lock(&proto_completion_mutex_);
  auto new_value = proto_ready_responders_.emplace(
      request->proto_id,
      ProtoReadyResponse{
          .arena = std::move(arena),
          .completer = completer.ToAsync(),
          .timeout_task = std::make_unique<async::Task>(
              [this, proto_id = request->proto_id](async_dispatcher_t*, async::Task*, zx_status_t) {
                fbl::AutoLock lock(&proto_completion_mutex_);
                auto entry = proto_ready_responders_.find(proto_id);
                // Either the protocol was registered, and we won't find this entry, or it won't
                // have been and we'll have to report failure.
                if (entry != proto_ready_responders_.end()) {
                  auto& response = entry->second;
                  response.completer.buffer(response.arena).ReplyError(ZX_ERR_TIMED_OUT);
                  proto_ready_responders_.erase(entry);
                }
              }),
      });

  // Post the task.
  new_value.first->second.timeout_task->PostDelayed(
      fdf::Dispatcher::GetCurrent()->async_dispatcher(), zx::sec(100));
}

void PlatformBus::GetBoardName(GetBoardNameCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  // Reply immediately if board_name is valid.
  if (!board_info_.board_name().empty()) {
    completer.Reply(ZX_OK, fidl::StringView::FromExternal(board_info_.board_name()));
    return;
  }
  // Cache the requests until board_name becomes valid.
  board_name_completer_.push_back(completer.ToAsync());
}

void PlatformBus::GetBoardRevision(GetBoardRevisionCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  completer.Reply(ZX_OK, board_info_.board_revision());
}

void PlatformBus::GetBootloaderVendor(GetBootloaderVendorCompleter::Sync& completer) {
  fbl::AutoLock lock(&bootloader_info_lock_);
  // Reply immediately if vendor is valid.
  if (bootloader_info_.vendor() != std::nullopt) {
    completer.Reply(ZX_OK, fidl::StringView::FromExternal(bootloader_info_.vendor().value()));
    return;
  }
  // Cache the requests until vendor becomes valid.
  bootloader_vendor_completer_.push_back(completer.ToAsync());
}

void PlatformBus::GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync& completer) {
  fuchsia_sysinfo::wire::InterruptControllerInfo info = {
      .type = interrupt_controller_type_,
  };
  completer.Reply(
      ZX_OK, fidl::ObjectView<fuchsia_sysinfo::wire::InterruptControllerInfo>::FromExternal(&info));
}

void PlatformBus::GetBoardInfo(fdf::Arena& arena, GetBoardInfoCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  fidl::Arena<> fidl_arena;
  completer.buffer(arena).ReplySuccess(fidl::ToWire(fidl_arena, board_info_));
}

void PlatformBus::SetBoardInfo(SetBoardInfoRequestView request, fdf::Arena& arena,
                               SetBoardInfoCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  auto& info = request->info;
  if (info.has_board_name()) {
    board_info_.board_name() = info.board_name().get();
    zxlogf(INFO, "PlatformBus: set board name to \"%s\"", board_info_.board_name().data());

    std::vector<GetBoardNameCompleter::Async> completer_tmp_;
    // Respond to pending boardname requests, if any.
    board_name_completer_.swap(completer_tmp_);
    while (!completer_tmp_.empty()) {
      completer_tmp_.back().Reply(ZX_OK, fidl::StringView::FromExternal(board_info_.board_name()));
      completer_tmp_.pop_back();
    }
  }
  if (info.has_board_revision()) {
    board_info_.board_revision() = info.board_revision();
  }
  completer.buffer(arena).ReplySuccess();
}

void PlatformBus::SetBootloaderInfo(SetBootloaderInfoRequestView request, fdf::Arena& arena,
                                    SetBootloaderInfoCompleter::Sync& completer) {
  fbl::AutoLock lock(&bootloader_info_lock_);
  auto& info = request->info;
  if (info.has_vendor()) {
    bootloader_info_.vendor() = info.vendor().get();
    zxlogf(INFO, "PlatformBus: set bootloader vendor to \"%s\"", bootloader_info_.vendor()->data());

    std::vector<GetBootloaderVendorCompleter::Async> completer_tmp_;
    // Respond to pending boardname requests, if any.
    bootloader_vendor_completer_.swap(completer_tmp_);
    while (!completer_tmp_.empty()) {
      completer_tmp_.back().Reply(
          ZX_OK, fidl::StringView::FromExternal(bootloader_info_.vendor().value()));
      completer_tmp_.pop_back();
    }
  }
  completer.buffer(arena).ReplySuccess();
}

void PlatformBus::RegisterSysSuspendCallback(RegisterSysSuspendCallbackRequestView request,
                                             fdf::Arena& arena,
                                             RegisterSysSuspendCallbackCompleter::Sync& completer) {
  suspend_cb_.Bind(std::move(request->suspend_cb), fdf::Dispatcher::GetCurrent()->get());
  completer.buffer(arena).ReplySuccess();
}

namespace {
struct CompositeFragmentData {
  std::vector<std::vector<zx_bind_inst_t>> match_programs;
  std::vector<device_fragment_part_t> fragment_parts;
  std::string name;
};

// Given |desc|, populates |fragments| with fragments. The return value contains ancillary data for
// the fragment definitions and must live as long as the |fragments| array is used.
zx::status<std::vector<CompositeFragmentData>> ConvertFidlFragments(
    fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment> fragments_list,
    cpp20::span<device_fragment_t> fragments) {
  if (fragments_list.count() > fragments.size()) {
    zxlogf(ERROR, "Too many fragments requested.");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  std::vector<CompositeFragmentData> ret(fragments_list.count());
  // Convert the FIDL composite definition to a C one for the driver framework.
  for (size_t i = 0; i < fragments_list.count(); i++) {
    CompositeFragmentData& data = ret[i];
    // How many fragment parts (and match programs) are there?
    const size_t num_parts = fragments_list[i].parts.count();
    data.fragment_parts.resize(num_parts);
    data.match_programs.resize(num_parts);
    // Store the fragment name as a null-terminated string.
    data.name = fragments_list[i].name.get();

    auto& parts = data.fragment_parts;
    auto& programs = data.match_programs;

    // For each part...
    for (size_t j = 0; j < num_parts; j++) {
      // Convert the match program to the C zx_bind_inst_t.
      auto program = fragments_list[i].parts[j].match_program;
      std::vector<zx_bind_inst_t> dst{program.count()};
      for (size_t k = 0; k < program.count(); k++) {
        dst[k].arg = program[k].arg;
        dst[k].debug = program[k].debug;
        dst[k].op = program[k].op;
      }

      // Store the program in the vector and create a device_fragment_part_t.
      programs[j] = std::move(dst);
      parts[j] = device_fragment_part_t{
          .instruction_count = static_cast<uint32_t>(programs[j].size()),
          .match_program = programs[j].data(),
      };
    }

    // Update the fragment info after we've converted all of the parts.
    fragments[i].name = data.name.data();
    fragments[i].parts_count = static_cast<uint32_t>(num_parts);
    fragments[i].parts = data.fragment_parts.data();
  }

  return zx::ok(std::move(ret));
}
}  // namespace

void PlatformBus::AddCompositeImplicitPbusFragment(
    AddCompositeImplicitPbusFragmentRequestView request, fdf::Arena& arena,
    AddCompositeImplicitPbusFragmentCompleter::Sync& completer) {
  if (!request->node.has_name()) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto fragments_list = request->fragments;
  std::string primary_fragment(request->primary_fragment.get());

  // Do not allow adding composite devices in our driver host.
  // |primary_fragment| must be nullptr to spawn in a new driver host or equal to one of the
  // fragments names to spawn in the same driver host as it.
  if (primary_fragment == "pdev") {
    zxlogf(ERROR, "%s: primary_fragment cannot be pdev", __func__);
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto natural = fidl::ToNatural(request->node);
  auto valid = ValidateResources(natural);
  if (valid.is_error()) {
    completer.buffer(arena).ReplyError(valid.status_value());
    return;
  }

  auto status =
      PlatformDevice::Create(std::move(natural), zxdev(), this, PlatformDevice::Fragment, &dev);
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }

  status = dev->Start();
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  constexpr size_t kMaxFragments = 100;
  if (fragments_list.count() + 1 > kMaxFragments) {
    zxlogf(ERROR, "Too many fragments requested.");
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  device_fragment_t fragments[kMaxFragments];

  const zx_bind_inst_t pdev_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, request->node.has_vid() ? request->node.vid() : 0),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, request->node.has_pid() ? request->node.pid() : 0),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, request->node.has_did() ? request->node.did() : 0),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_INSTANCE_ID,
                  request->node.has_instance_id() ? request->node.instance_id() : 0),
  };
  const device_fragment_part_t pdev_fragment[] = {
      {std::size(pdev_match), pdev_match},
  };

  fragments[0].name = "pdev";
  fragments[0].parts_count = std::size(pdev_fragment);
  fragments[0].parts = pdev_fragment;

  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, request->node.has_vid() ? request->node.vid() : 0},
      {BIND_PLATFORM_DEV_PID, 0, request->node.has_pid() ? request->node.pid() : 0},
      {BIND_PLATFORM_DEV_DID, 0, request->node.has_did() ? request->node.did() : 0},
      {BIND_PLATFORM_DEV_INSTANCE_ID, 0,
       request->node.has_instance_id() ? request->node.instance_id() : 0},
  };

  auto ret =
      ConvertFidlFragments(request->fragments, cpp20::span(&fragments[1], kMaxFragments - 1));
  if (ret.is_error()) {
    completer.buffer(arena).ReplyError(ret.error_value());
    return;
  }

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = fragments,
      .fragments_count = fragments_list.count() + 1,
      .primary_fragment = request->primary_fragment.is_null() ? "pdev" : primary_fragment.data(),
      .spawn_colocated = request->primary_fragment.is_null(),
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite(std::string(request->node.name().get()).data(), &comp_desc);
  completer.buffer(arena).Reply(zx::make_status(status));
}

void PlatformBus::AddComposite(AddCompositeRequestView request, fdf::Arena& arena,
                               AddCompositeCompleter::Sync& completer) {
  auto pdev = request->node;
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, pdev.has_vid() ? pdev.vid() : 0},
      {BIND_PLATFORM_DEV_PID, 0, pdev.has_pid() ? pdev.pid() : 0},
      {BIND_PLATFORM_DEV_DID, 0, pdev.has_did() ? pdev.did() : 0},
      {BIND_PLATFORM_DEV_INSTANCE_ID, 0, pdev.has_instance_id() ? pdev.instance_id() : 0},
  };

  std::vector<device_fragment_t> fragments(request->fragments.count());
  std::string primary_fragment(request->primary_fragment.get());

  auto ret = ConvertFidlFragments(request->fragments, cpp20::span(fragments));
  if (ret.is_error()) {
    completer.buffer(arena).ReplyError(ret.error_value());
    return;
  }

  const bool is_primary_pdev = primary_fragment == "pdev";
  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = fragments.data(),
      .fragments_count = fragments.size(),
      .primary_fragment = primary_fragment.data(),
      .spawn_colocated = !is_primary_pdev,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };
  if (!request->node.has_name()) {
    completer.buffer(arena).ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = DdkAddComposite(std::string(request->node.name().get()).data(), &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    completer.buffer(arena).ReplyError(status);
    return;
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto natural = fidl::ToNatural(request->node);
  auto valid = ValidateResources(natural);
  if (valid.is_error()) {
    completer.buffer(arena).ReplyError(valid.error_value());
    return;
  }
  status =
      PlatformDevice::Create(std::move(natural), zxdev(), this, PlatformDevice::Fragment, &dev);
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }
  status = dev->Start();
  if (status != ZX_OK) {
    completer.buffer(arena).ReplyError(status);
    return;
  }
  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  completer.buffer(arena).ReplySuccess();
}

zx_status_t PlatformBus::DdkGetProtocol(uint32_t proto_id, void* out) {
  switch (proto_id) {
    // DO NOT ADD ANY MORE PROTOCOLS HERE.
    // GPIO_IMPL is needed for board driver pinmuxing. IOMMU is for potential future use.
    // CLOCK_IMPL are needed by the amlogic board drivers. Use of this mechanism for all other
    // protocols has been deprecated.
    case ZX_PROTOCOL_CLOCK_IMPL:
      if (clock_) {
        clock_->GetProto(static_cast<clock_impl_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_GPIO_IMPL:
      if (gpio_) {
        gpio_->GetProto(static_cast<gpio_impl_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_IOMMU:
      if (iommu_) {
        iommu_->GetProto(static_cast<iommu_protocol_t*>(out));
        return ZX_OK;
      } else {
        // return default implementation
        auto proto = static_cast<iommu_protocol_t*>(out);
        proto->ctx = this;
        proto->ops = &iommu_protocol_ops_;
        return ZX_OK;
      }
      break;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx::status<PlatformBus::BootItemResult> PlatformBus::GetBootItem(uint32_t type, uint32_t extra) {
  auto result = fidl::WireCall(items_svc_)->Get(type, extra);
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (!result->payload.is_valid()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(PlatformBus::BootItemResult{
      .vmo = std::move(result->payload),
      .length = result->length,
  });
}

zx::status<fbl::Array<uint8_t>> PlatformBus::GetBootItemArray(uint32_t type, uint32_t extra) {
  zx::status result = GetBootItem(type, extra);
  if (result.is_error()) {
    return result.take_error();
  }
  auto& [vmo, length] = *result;
  fbl::Array<uint8_t> data(new uint8_t[length], length);
  zx_status_t status = vmo.read(data.data(), 0, data.size());
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(data));
}

void PlatformBus::DdkRelease() { delete this; }

typedef struct {
  void* pbus_instance;
  zx_device_t* sys_root;
} sysdev_suspend_t;

static void sys_device_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                               uint8_t suspend_reason) {
  auto* p = reinterpret_cast<sysdev_suspend_t*>(ctx);
  auto* pbus = reinterpret_cast<class PlatformBus*>(p->pbus_instance);

  if (pbus != nullptr) {
    auto& suspend_cb = pbus->suspend_cb();
    if (suspend_cb.is_valid()) {
      fdf::Arena arena('SUSP');

      suspend_cb.buffer(arena)
          ->Callback(requested_state, enable_wake, suspend_reason)
          .ThenExactlyOnce(
              [sys_root = p->sys_root](
                  fdf::WireUnownedResult<fuchsia_hardware_platform_bus::SysSuspend::Callback>&
                      status) {
                if (!status.ok()) {
                  device_suspend_reply(sys_root, status.status(), DEV_POWER_STATE_D0);
                  return;
                }
                device_suspend_reply(sys_root, status->out_status, status->out_state);
              });
      return;
    }
  }
  device_suspend_reply(p->sys_root, ZX_OK, 0);
}

static void sys_device_release(void* ctx) {
  auto* p = reinterpret_cast<sysdev_suspend_t*>(ctx);
  delete p;
}

// cpu-trace provides access to the cpu's tracing and performance counters.
// As such the "device" is the cpu itself.
static void InitCpuTrace(zx_device_t* parent, const zx::iommu& dummy_iommu) {
  zx::bti cpu_trace_bti;
  zx_status_t status = zx::bti::create(dummy_iommu, 0, CPU_TRACE_BTI_ID, &cpu_trace_bti);
  if (status != ZX_OK) {
    // This is not fatal.
    zxlogf(ERROR, "platform-bus: error %d in bti_create(cpu_trace_bti)", status);
    return;
  }

  status = publish_cpu_trace(cpu_trace_bti.release(), parent);
  if (status != ZX_OK) {
    // This is not fatal.
    zxlogf(INFO, "publish_cpu_trace returned %d", status);
  }
}

static zx_protocol_device_t sys_device_proto = []() {
  zx_protocol_device_t result = {};

  result.version = DEVICE_OPS_VERSION;
  result.suspend = sys_device_suspend;
  result.release = sys_device_release;
  return result;
}();

zx_status_t PlatformBus::Create(zx_device_t* parent, const char* name, zx::channel items_svc) {
  // This creates the "sys" device.
  sys_device_proto.version = DEVICE_OPS_VERSION;

  // The suspend op needs to get access to the PBus instance, to be able to
  // callback the ACPI suspend hook. Introducing a level of indirection here
  // to allow us to update the PBus instance in the device context after creating
  // the device.
  fbl::AllocChecker ac;
  std::unique_ptr<sysdev_suspend_t> suspend(new (&ac) sysdev_suspend_t);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  suspend->pbus_instance = nullptr;

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "sys",
      .ctx = suspend.get(),
      .ops = &sys_device_proto,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  // Create /dev/sys.
  if (zx_status_t status = device_add(parent, &args, &suspend->sys_root); status != ZX_OK) {
    return status;
  }
  sysdev_suspend_t* suspend_ptr = suspend.release();

  // Add child of sys for the board driver to bind to.
  std::unique_ptr<platform_bus::PlatformBus> bus(
      new (&ac) platform_bus::PlatformBus(suspend_ptr->sys_root, std::move(items_svc)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (zx_status_t status = bus->Init(); status != ZX_OK) {
    zxlogf(ERROR, "failed to init: %s", zx_status_get_string(status));
    return status;
  }
  // devmgr is now in charge of the device.
  platform_bus::PlatformBus* bus_ptr = bus.release();
  suspend_ptr->pbus_instance = bus_ptr;

  // Create /dev/sys/cpu-trace.
  // But only do so if we have an iommu handle. Normally we do, but tests
  // may create us without a root resource, and thus without the iommu
  // handle.
  if (bus_ptr->iommu_handle_.is_valid()) {
    // Failure is not fatal. Error message already printed.
    InitCpuTrace(suspend_ptr->sys_root, bus_ptr->iommu_handle_);
  }

  return ZX_OK;
}

PlatformBus::PlatformBus(zx_device_t* parent, zx::channel items_svc)
    : PlatformBusType(parent),
      items_svc_(fidl::ClientEnd<fuchsia_boot::Items>(std::move(items_svc))),
      outgoing_(driver::OutgoingDirectory::Create(fdf::Dispatcher::GetCurrent()->get())) {}

zx::status<zbi_board_info_t> PlatformBus::GetBoardInfo() {
  zx::status result = GetBootItem(ZBI_TYPE_DRV_BOARD_INFO, 0);
  if (result.is_error()) {
    // This is expected on some boards.
    zxlogf(INFO, "Boot Item ZBI_TYPE_DRV_BOARD_INFO not found");
    return result.take_error();
  }
  auto& [vmo, length] = *result;
  if (length != sizeof(zbi_board_info_t)) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  zbi_board_info_t board_info;
  zx_status_t status = vmo.read(&board_info, 0, length);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read zbi_board_info_t VMO");
    return zx::error(status);
  }
  return zx::ok(board_info);
}

zx_status_t PlatformBus::Init() {
  zx_status_t status;
  // Set up a dummy IOMMU protocol to use in the case where our board driver
  // does not set a real one.
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  if (root_resource->is_valid()) {
    status =
        zx::iommu::create(*root_resource, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu_handle_);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Read kernel driver.
#if __x86_64__
  interrupt_controller_type_ = fuchsia_sysinfo::wire::InterruptControllerType::kApic;
#else
  auto boot_item = GetBootItem(ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GIC_V2);
  if (boot_item.is_error() && boot_item.status_value() != ZX_ERR_NOT_FOUND) {
    return boot_item.status_value();
  }
  if (boot_item.is_ok()) {
    interrupt_controller_type_ = fuchsia_sysinfo::wire::InterruptControllerType::kGicV2;
  }
  boot_item = GetBootItem(ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GIC_V3);
  if (boot_item.is_error() && boot_item.status_value() != ZX_ERR_NOT_FOUND) {
    return boot_item.status_value();
  }
  if (boot_item.is_ok()) {
    interrupt_controller_type_ = fuchsia_sysinfo::wire::InterruptControllerType::kGicV3;
  }
#endif

  // Read platform ID.
  zx::status platform_id_result = GetBootItem(ZBI_TYPE_PLATFORM_ID, 0);
  if (platform_id_result.is_error() && platform_id_result.status_value() != ZX_ERR_NOT_FOUND) {
    return platform_id_result.status_value();
  }

#if __aarch64__
  {
    // For arm64, we do not expect a board to set the bootloader info.
    fbl::AutoLock lock(&bootloader_info_lock_);
    bootloader_info_.vendor() = "<unknown>";
  }
#endif

  fbl::AutoLock lock(&board_info_lock_);
  if (platform_id_result.is_ok()) {
    if (platform_id_result->length != sizeof(zbi_platform_id_t)) {
      return ZX_ERR_INTERNAL;
    }
    zbi_platform_id_t platform_id;
    status = platform_id_result->vmo.read(&platform_id, 0, sizeof(platform_id));
    if (status != ZX_OK) {
      return status;
    }
    zxlogf(INFO, "platform bus: VID: %u PID: %u board: \"%s\"", platform_id.vid, platform_id.pid,
           platform_id.board_name);
    board_info_.vid() = platform_id.vid;
    board_info_.pid() = platform_id.pid;
    board_info_.board_name() = platform_id.board_name;
  } else {
#if __x86_64__
    // For x64, we might not find the ZBI_TYPE_PLATFORM_ID, old bootloaders
    // won't support this, for example. If this is the case, cons up the VID/PID
    // here to allow the acpi board driver to load and bind.
    board_info_.vid() = PDEV_VID_INTEL;
    board_info_.pid() = PDEV_PID_X86;
#else
    zxlogf(ERROR, "platform_bus: ZBI_TYPE_PLATFORM_ID not found");
    return ZX_ERR_INTERNAL;
#endif
  }

  // Set default board_revision.
  zx::status zbi_board_info = GetBoardInfo();
  if (zbi_board_info.is_ok()) {
    board_info_.board_revision() = zbi_board_info->revision;
  }

  // Then we attach the platform-bus device below it.
  status = DdkAdd(ddk::DeviceAddArgs("platform").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  zx_device_prop_t passthrough_props[] = {
      {BIND_FIDL_PROTOCOL, 0, ZX_FIDL_PROTOCOL_PLATFORM_BUS},
      {BIND_PLATFORM_DEV_VID, 0, board_info_.vid()},
      {BIND_PLATFORM_DEV_PID, 0, board_info_.pid()},
  };
  status = AddProtocolPassthrough("platform-passthrough", passthrough_props, this,
                                  &protocol_passthrough_);
  if (status != ZX_OK) {
    // We log the error but we do nothing as we've already added the device successfully.
    zxlogf(ERROR, "Error while adding platform-passthrough: %s", zx_status_get_string(status));
  }
  return ZX_OK;
}

zx::status<> PlatformBus::ValidateResources(fuchsia_hardware_platform_bus::Node& node) {
  if (node.name() == std::nullopt) {
    zxlogf(ERROR, "Node has no name?");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (node.mmio() != std::nullopt) {
    for (size_t i = 0; i < node.mmio()->size(); i++) {
      if (!IsValid(node.mmio().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid mmio %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  if (node.irq() != std::nullopt) {
    for (size_t i = 0; i < node.irq()->size(); i++) {
      if (!IsValid(node.irq().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid irq %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  if (node.bti() != std::nullopt) {
    for (size_t i = 0; i < node.bti()->size(); i++) {
      if (!IsValid(node.bti().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid bti %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  if (node.smc() != std::nullopt) {
    for (size_t i = 0; i < node.smc()->size(); i++) {
      if (!IsValid(node.smc().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid smc %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  if (node.metadata() != std::nullopt) {
    for (size_t i = 0; i < node.metadata()->size(); i++) {
      if (!IsValid(node.metadata().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid metadata %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  if (node.boot_metadata() != std::nullopt) {
    for (size_t i = 0; i < node.boot_metadata()->size(); i++) {
      if (!IsValid(node.boot_metadata().value()[i])) {
        zxlogf(ERROR, "node '%s' has invalid boot metadata %zu", node.name()->data(), i);
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }
  return zx::ok();
}

void PlatformBus::DdkInit(ddk::InitTxn txn) {
  zx::status board_data = GetBootItemArray(ZBI_TYPE_DRV_BOARD_PRIVATE, 0);
  if (board_data.is_error() && board_data.status_value() != ZX_ERR_NOT_FOUND) {
    return txn.Reply(board_data.status_value());
  }
  if (board_data.is_ok()) {
    zx_status_t status = device_add_metadata(protocol_passthrough_, DEVICE_METADATA_BOARD_PRIVATE,
                                             board_data->data(), board_data->size());
    if (status != ZX_OK) {
      return txn.Reply(status);
    }
  }
  fuchsia_hardware_platform_bus::Node device = {};
  device.name() = "ram-disk";
  device.vid() = PDEV_VID_GENERIC;
  device.pid() = PDEV_PID_GENERIC;
  device.did() = PDEV_DID_RAM_DISK;
  auto status = NodeAddInternal(device);
  if (status.is_error()) {
    return txn.Reply(status.error_value());
  }
  device.name() = "ram-nand";
  device.vid() = PDEV_VID_GENERIC;
  device.pid() = PDEV_PID_GENERIC;
  device.did() = PDEV_DID_RAM_NAND;
  status = NodeAddInternal(device);
  if (status.is_error()) {
    return txn.Reply(status.error_value());
  }
  device.name() = "virtual-audio";
  device.vid() = PDEV_VID_GENERIC;
  device.pid() = PDEV_PID_GENERIC;
  device.did() = PDEV_DID_VIRTUAL_AUDIO;
  status = NodeAddInternal(device);
  if (status.is_error()) {
    return txn.Reply(status.error_value());
  }
  device.name() = "bt-hci-emulator";
  device.vid() = PDEV_VID_GENERIC;
  device.pid() = PDEV_PID_GENERIC;
  device.did() = PDEV_DID_BT_HCI_EMULATOR;
  status = NodeAddInternal(device);
  if (status.is_error()) {
    return txn.Reply(status.error_value());
  }

  return txn.Reply(ZX_OK);  // This will make the device visible and able to be unbound.
}

zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                                zx_handle_t handle) {
  return platform_bus::PlatformBus::Create(parent, name, zx::channel(handle));
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = platform_bus_create;
  return ops;
}();

}  // namespace platform_bus

ZIRCON_DRIVER(platform_bus, platform_bus::driver_ops, "zircon", "0.1");
