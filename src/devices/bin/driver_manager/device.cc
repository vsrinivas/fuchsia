// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device.h"

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.driver.test.logger/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/coding.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <string_view>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/utf_codecs.h"

// TODO(fxbug.dev/43370): remove this once init tasks can be enabled for all devices.
static constexpr bool kEnableAlwaysInit = false;

Device::Device(Coordinator* coord, fbl::String name, fbl::String libname, fbl::String args,
               fbl::RefPtr<Device> parent, uint32_t protocol_id, zx::vmo inspect_vmo,
               zx::channel client_remote, fidl::ClientEnd<fio::Directory> outgoing_dir)
    : coordinator(coord),
      name_(std::move(name)),
      libname_(std::move(libname)),
      args_(std::move(args)),
      parent_(std::move(parent)),
      protocol_id_(protocol_id),
      publish_task_([this] { coordinator->device_manager()->HandleNewDevice(fbl::RefPtr(this)); }),
      client_remote_(std::move(client_remote)),
      outgoing_dir_(std::move(outgoing_dir)) {
  inspect_.emplace(coord->inspect_manager().devices(), coord->inspect_manager().device_count(),
                   name_.c_str(), std::move(inspect_vmo));
  set_state(Device::State::kActive);  // set default state
}

Device::~Device() {
  // Ideally we'd assert here that immortal devices are never destroyed, but
  // they're destroyed when the Coordinator object is cleaned up in tests.
  // We can probably get rid of the IMMORTAL flag, since if the Coordinator is
  // holding a reference we shouldn't be able to hit that check, in which case
  // the flag is only used to modify the proxy library loading behavior.

  devfs_unpublish(this);

  // If we destruct early enough, we may have created the core devices and devfs might not exist
  // yet.
  if (coordinator->inspect_manager().devfs()) {
    coordinator->inspect_manager().devfs()->Unpublish(this);
  }

  // Drop our reference to our driver_host if we still have it
  set_host(nullptr);

  // TODO: cancel any pending rpc responses
  // TODO: Have dtor assert that DEV_CTX_IMMORTAL set on flags
  VLOGF(1, "Destroyed device %p '%s'", this, name_.data());
}

void Device::Bind(fbl::RefPtr<Device> dev, async_dispatcher_t* dispatcher,
                  fidl::ServerEnd<fuchsia_device_manager::Coordinator> request) {
  dev->coordinator_binding_ = fidl::BindServer(
      dispatcher, std::move(request), dev.get(),
      [dev](Device* self, fidl::UnbindInfo info,
            fidl::ServerEnd<fuchsia_device_manager::Coordinator> server_end) {
        if (info.is_user_initiated()) {
          return;
        }
        if (info.is_peer_closed()) {
          // If the device is already dead, we are detecting an expected disconnect from the
          // driver_host.
          if (dev->state() != Device::State::kDead) {
            // TODO(https://fxbug.dev/56208): Change this log back to error once isolated devmgr
            // is fixed.
            LOGF(WARNING, "Disconnected device %p '%s', see fxbug.dev/56208 for potential cause",
                 dev.get(), dev->name().data());

            dev->coordinator->device_manager()->RemoveDevice(dev, true);
          }
          return;
        }
        LOGF(ERROR, "Failed to handle RPC for device %p '%s': %s", dev.get(), dev->name().data(),
             info.FormatDescription().c_str());
      });
}

zx_status_t Device::Create(
    Coordinator* coordinator, const fbl::RefPtr<Device>& parent, fbl::String name,
    fbl::String driver_path, fbl::String args, uint32_t protocol_id,
    fbl::Array<zx_device_prop_t> props, fbl::Array<StrProperty> str_props,
    fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
    fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
    bool want_init_task, bool skip_autobind, zx::vmo inspect, zx::channel client_remote,
    fidl::ClientEnd<fio::Directory> outgoing_dir, fbl::RefPtr<Device>* device) {
  fbl::RefPtr<Device> real_parent;
  // If our parent is a proxy, for the purpose of devfs, we need to work with
  // *its* parent which is the device that it is proxying.
  if (parent->flags & DEV_CTX_PROXY) {
    real_parent = parent->parent();
  } else {
    real_parent = parent;
  }

  auto dev = fbl::MakeRefCounted<Device>(
      coordinator, std::move(name), std::move(driver_path), std::move(args), real_parent,
      protocol_id, std::move(inspect), std::move(client_remote), std::move(outgoing_dir));
  if (!dev) {
    return ZX_ERR_NO_MEMORY;
  }

  if (skip_autobind) {
    dev->flags |= DEV_CTX_SKIP_AUTOBIND;
  }

  // Initialise and publish device inspect
  if (auto status = coordinator->inspect_manager().devfs()->InitInspectFileAndPublish(dev);
      status.is_error()) {
    return status.error_value();
  }

  zx_status_t status = dev->SetProps(std::move(props));
  if (status != ZX_OK) {
    return status;
  }

  if (auto status = dev->SetStrProps(std::move(str_props)); status != ZX_OK) {
    return status;
  }

  dev->device_controller_.Bind(std::move(device_controller), coordinator->dispatcher());
  Device::Bind(dev, coordinator->dispatcher(), std::move(coordinator_request));

  // If we have bus device args we are, by definition, a bus device.
  if (dev->args_.size() > 0) {
    dev->flags |= DEV_CTX_BUS_DEVICE | DEV_CTX_MUST_ISOLATE;
  }

  if (dev->has_outgoing_directory()) {
    dev->flags |= DEV_CTX_MUST_ISOLATE;
  }

  // We exist within our parent's device host
  dev->set_host(parent->host());

  // We must mark the device as invisible before publishing so
  // that we don't send "device added" notifications.
  // The init task must complete before marking the device visible.
  if (want_init_task) {
    dev->flags |= DEV_CTX_INVISIBLE;
  }

  if ((status = devfs_publish(real_parent, dev)) < 0) {
    return status;
  }

  real_parent->children_.push_back(dev.get());
  VLOGF(1, "Created device %p '%s' (child)", real_parent.get(), real_parent->name().data());

  if (want_init_task) {
    dev->CreateInitTask();
  }

  dev->InitializeInspectValues();

  *device = std::move(dev);
  return ZX_OK;
}

zx_status_t Device::CreateComposite(
    Coordinator* coordinator, fbl::RefPtr<DriverHost> driver_host, const CompositeDevice& composite,
    fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
    fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
    fbl::RefPtr<Device>* device) {
  const auto& composite_props = composite.properties();
  fbl::Array<zx_device_prop_t> props(new zx_device_prop_t[composite_props.size()],
                                     composite_props.size());
  memcpy(props.data(), composite_props.data(), props.size() * sizeof(props[0]));

  const auto& composite_str_props = composite.str_properties();
  fbl::Array<StrProperty> str_props(new StrProperty[composite_str_props.size()],
                                    composite_str_props.size());
  for (size_t i = 0; i < composite_str_props.size(); i++) {
    str_props[i] = composite_str_props[i];
  }

  auto dev = fbl::MakeRefCounted<Device>(coordinator, composite.name(), fbl::String(),
                                         fbl::String(), nullptr, 0, zx::vmo(), zx::channel(),
                                         fidl::ClientEnd<fio::Directory>());
  if (!dev) {
    return ZX_ERR_NO_MEMORY;
  }

  if (auto status = coordinator->inspect_manager().devfs()->InitInspectFileAndPublish(dev);
      status.is_error()) {
    return status.error_value();
  }

  zx_status_t status = dev->SetProps(std::move(props));
  if (status != ZX_OK) {
    return status;
  }

  status = dev->SetStrProps(std::move(str_props));
  if (status != ZX_OK) {
    return status;
  }

  dev->device_controller_.Bind(std::move(device_controller), coordinator->dispatcher());
  Device::Bind(dev, coordinator->dispatcher(), std::move(coordinator_request));
  // We exist within our parent's device host
  dev->set_host(std::move(driver_host));

  // TODO: Record composite membership

  // TODO(teisenbe): Figure out how to manifest in devfs?  For now just hang it off of
  // the root device?
  if ((status = devfs_publish(coordinator->root_device(), dev)) < 0) {
    return status;
  }

  VLOGF(1, "Created composite device %p '%s'", dev.get(), dev->name().data());

  dev->InitializeInspectValues();
  *device = std::move(dev);
  return ZX_OK;
}

zx_status_t Device::CreateProxy() {
  ZX_ASSERT(proxy_ == nullptr);

  fbl::String driver_path = libname_;
  // non-immortal devices, use foo.proxy.so for
  // their proxy devices instead of foo.so
  if (!(this->flags & DEV_CTX_IMMORTAL)) {
    const char* begin = driver_path.data();
    const char* end = strstr(begin, ".so");
    std::string_view prefix(begin, end == nullptr ? driver_path.size() : end - begin);
    driver_path = fbl::String::Concat({prefix, ".proxy.so"});
    // If the proxy is a URL we have to load it first.
    if (driver_path[0] != '/') {
      std::string string(driver_path.data());
      coordinator->driver_loader().LoadDriverUrl(string);
    }
  }

  auto dev = fbl::MakeRefCounted<Device>(this->coordinator, name_, std::move(driver_path),
                                         fbl::String(), fbl::RefPtr(this), protocol_id_, zx::vmo(),
                                         zx::channel(), fidl::ClientEnd<fio::Directory>());
  if (dev == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  dev->flags = DEV_CTX_PROXY;

  dev->InitializeInspectValues();

  proxy_ = std::move(dev);
  VLOGF(1, "Created proxy device %p '%s'", this, name_.data());
  return ZX_OK;
}

zx_status_t Device::CreateNewProxy() {
  ZX_ASSERT(new_proxy_ == nullptr);

  auto dev = fbl::MakeRefCounted<Device>(this->coordinator, name_, fbl::String(), fbl::String(),
                                         fbl::RefPtr(this), 0, zx::vmo(), zx::channel(),
                                         fidl::ClientEnd<fio::Directory>());
  if (dev == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  dev->flags = DEV_CTX_PROXY;

  dev->InitializeInspectValues();

  new_proxy_ = std::move(dev);
  VLOGF(1, "Created new_proxy device %p '%s'", this, name_.data());
  return ZX_OK;
}

void Device::InitializeInspectValues() {
  inspect().set_driver(libname().c_str());
  inspect().set_protocol_id(protocol_id_);
  inspect().set_flags(flags);
  inspect().set_properties(props());

  char path[fuchsia_device_manager::wire::kDevicePathMax] = {};
  coordinator->GetTopologicalPath(fbl::RefPtr(this), path,
                                  fuchsia_device_manager::wire::kDevicePathMax);
  inspect().set_topological_path(path);

  std::string type("Device");
  if (flags & DEV_CTX_PROXY) {
    type = std::string("Proxy device");
  } else if (is_composite()) {
    type = std::string("Composite device");
  }
  inspect().set_type(type);
}

void Device::DetachFromParent() {
  // Do this first as we might be deleting the last reference to ourselves.
  auto parent = std::move(parent_);
  if (this->flags & DEV_CTX_PROXY) {
    parent->proxy_ = nullptr;
  } else {
    parent->children_.erase(*this);
  }
}

zx_status_t Device::SignalReadyForBind(zx::duration delay) {
  return publish_task_.PostDelayed(this->coordinator->dispatcher(), delay);
}

void Device::CreateInitTask() {
  // We only ever create an init task when a device is initially added.
  ZX_ASSERT(!active_init_);
  set_state(Device::State::kInitializing);
  active_init_ = InitTask::Create(fbl::RefPtr(this));
}

fbl::RefPtr<SuspendTask> Device::RequestSuspendTask(uint32_t suspend_flags) {
  if (active_suspend_) {
    // We don't support different types of suspends concurrently, and
    // shouldn't be able to reach this state.
    ZX_ASSERT(suspend_flags == active_suspend_->suspend_flags());
  } else {
    active_suspend_ = SuspendTask::Create(fbl::RefPtr(this), suspend_flags);
  }
  return active_suspend_;
}

void Device::SendInit(InitCompletion completion) {
  ZX_ASSERT(!init_completion_);

  VLOGF(1, "Initializing device %p '%s'", this, name_.data());
  device_controller()->Init().ThenExactlyOnce(
      [dev = fbl::RefPtr(this)](
          fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::Init>& result) {
        if (!result.ok()) {
          dev->CompleteInit(result.status());
          return;
        }
        auto* response = result.Unwrap();
        VLOGF(1, "Initialized device %p '%s': %s", dev.get(), dev->name().data(),
              zx_status_get_string(response->status));
        dev->CompleteInit(response->status);
      });
  init_completion_ = std::move(completion);
}

zx_status_t Device::CompleteInit(zx_status_t status) {
  if (!init_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when initializing device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  if (init_completion_) {
    init_completion_(status);
  }
  DropInitTask();
  return ZX_OK;
}

fbl::RefPtr<ResumeTask> Device::RequestResumeTask(uint32_t target_system_state) {
  if (active_resume_) {
    // We don't support different types of resumes concurrently, and
    // shouldn't be able to reach this state.
    ZX_ASSERT(target_system_state == active_resume_->target_system_state());
  } else {
    active_resume_ = ResumeTask::Create(fbl::RefPtr(this), target_system_state);
  }
  return active_resume_;
}

void Device::SendSuspend(uint32_t flags, SuspendCompletion completion) {
  if (suspend_completion_) {
    // We already have a pending suspend
    return completion(ZX_ERR_UNAVAILABLE);
  }
  VLOGF(1, "Suspending device %p '%s'", this, name_.data());
  device_controller()->Suspend(flags).ThenExactlyOnce(
      [dev = fbl::RefPtr(this)](
          fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::Suspend>& result) {
        if (!result.ok()) {
          dev->CompleteSuspend(result.status());
          return;
        }
        auto* response = result.Unwrap();
        if (response->status == ZX_OK) {
          LOGF(DEBUG, "Suspended device %p '%s' successfully", dev.get(), dev->name().data());
        } else {
          LOGF(ERROR, "Failed to suspended device %p '%s': %s", dev.get(), dev->name().data(),
               zx_status_get_string(response->status));
        }
        dev->CompleteSuspend(response->status);
      });
  set_state(Device::State::kSuspending);
  suspend_completion_ = std::move(completion);
}

void Device::SendResume(uint32_t target_system_state, ResumeCompletion completion) {
  if (resume_completion_) {
    // We already have a pending resume
    return completion(ZX_ERR_UNAVAILABLE);
  }
  VLOGF(1, "Resuming device %p '%s'", this, name_.data());

  device_controller()
      ->Resume(target_system_state)
      .ThenExactlyOnce(
          [dev = fbl::RefPtr(this)](
              fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::Resume>& result) {
            if (!result.ok()) {
              dev->CompleteResume(result.status());
              return;
            }
            auto* response = result.Unwrap();
            LOGF(INFO, "Resumed device %p '%s': %s", dev.get(), dev->name().data(),
                 zx_status_get_string(response->status));
            dev->CompleteResume(response->status);
          });
  set_state(Device::State::kResuming);
  resume_completion_ = std::move(completion);
}

void Device::CompleteSuspend(zx_status_t status) {
  // If a device is being removed, any existing suspend task will be forcibly completed,
  // in which case we should not update the state.
  if (state_ != Device::State::kDead) {
    if (status == ZX_OK) {
      set_state(Device::State::kSuspended);
    } else {
      set_state(Device::State::kActive);
    }
  }

  if (suspend_completion_) {
    suspend_completion_(status);
  }
  DropSuspendTask();
}

void Device::CompleteResume(zx_status_t status) {
  if (status == ZX_OK) {
    set_state(Device::State::kResumed);
  } else {
    set_state(Device::State::kSuspended);
  }
  if (resume_completion_) {
    resume_completion_(status);
  }
}

void Device::CreateUnbindRemoveTasks(UnbindTaskOpts opts) {
  if (state_ == Device::State::kDead) {
    return;
  }
  // Create the tasks if they do not exist yet. We always create both.
  if (active_unbind_ == nullptr && active_remove_ == nullptr) {
    // Make sure the remove task exists before the unbind task,
    // as the unbind task adds the remove task as a dependent.
    active_remove_ = RemoveTask::Create(fbl::RefPtr(this));
    active_unbind_ = UnbindTask::Create(fbl::RefPtr(this), opts);
    return;
  }
  if (!active_unbind_) {
    // The unbind task has already completed and the device is now being removed.
    return;
  }
  // User requested removals take priority over coordinator generated unbind tasks.
  bool override_existing = opts.driver_host_requested && !active_unbind_->driver_host_requested();
  if (!override_existing) {
    return;
  }
  // There is a potential race condition where a driver calls device_remove() on themselves
  // but the device's unbind hook is about to be called due to a parent being removed.
  // Since it is illegal to call device_remove() twice under the old API,
  // drivers handle this by checking whether their device has already been removed in
  // their unbind hook and hence will never reply to their unbind hook.
  if (state_ == Device::State::kUnbinding) {
    if (unbind_completion_) {
      zx_status_t status = CompleteUnbind(ZX_OK);
      if (status != ZX_OK) {
        LOGF(ERROR, "Cannot complete unbind: %s", zx_status_get_string(status));
      }
    }
  } else {
    // |do_unbind| may not match the stored field in the existing unbind task due to
    // the current device_remove / unbind model.
    // For closest compatibility with the current model, we should prioritize
    // driver_host calls to |ScheduleRemove| over our own scheduled unbind tasks for the children.
    active_unbind_->set_do_unbind(opts.do_unbind);
  }
}

void Device::SendUnbind(UnbindCompletion& completion) {
  if (unbind_completion_) {
    // We already have a pending unbind
    return completion(ZX_ERR_UNAVAILABLE);
  }
  VLOGF(1, "Unbinding device %p '%s'", this, name_.data());
  set_state(Device::State::kUnbinding);
  device_controller()->Unbind().ThenExactlyOnce(
      [dev = fbl::RefPtr(this)](
          fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::Unbind>& result) {
        if (!result.ok()) {
          dev->CompleteUnbind(result.status());
          return;
        }
        auto* response = result.Unwrap();
        LOGF(INFO, "Unbound device %p '%s': %s", dev.get(), dev->name().data(),
             zx_status_get_string(response->is_error() ? response->error_value() : ZX_OK));
        dev->CompleteUnbind();
      });
  unbind_completion_ = std::move(completion);
}

void Device::SendCompleteRemove(RemoveCompletion& completion) {
  if (remove_completion_) {
    // We already have a pending remove.
    return completion(ZX_ERR_UNAVAILABLE);
  }
  VLOGF(1, "Completing removal of device %p '%s'", this, name_.data());
  set_state(Device::State::kUnbinding);
  device_controller()->CompleteRemoval().ThenExactlyOnce(
      [dev = fbl::RefPtr(this)](
          fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::CompleteRemoval>&
              result) {
        if (!result.ok()) {
          if (dev->remove_completion_) {
            dev->remove_completion_(result.status());
          }
          dev->DropRemoveTask();
          return;
        }
        auto* response = result.Unwrap();
        LOGF(INFO, "Removed device %p '%s': %s", dev.get(), dev->name().data(),
             zx_status_get_string(response->is_error() ? response->error_value() : ZX_OK));
        dev->CompleteRemove();
      });
  remove_completion_ = std::move(completion);
}

zx_status_t Device::CompleteUnbind(zx_status_t status) {
  if (!unbind_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when unbinding device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  if (unbind_completion_) {
    unbind_completion_(status);
  }
  DropUnbindTask();
  return ZX_OK;
}

zx_status_t Device::CompleteRemove(zx_status_t status) {
  if (!remove_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when removing device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  // If we received an error, it is because we are currently force removing the device.
  if (status == ZX_OK) {
    coordinator->device_manager()->RemoveDevice(fbl::RefPtr(this), false);
  }
  if (remove_completion_) {
    // If we received an error, it is because we are currently force removing the device.
    // In that case, all other devices in the driver_host will be force removed too,
    // and they will call CompleteRemove() before the remove task is scheduled to run.
    // For ancestor dependents in other driver_hosts, we want them to proceed removal as usual.
    remove_completion_(ZX_OK);
  }
  DropRemoveTask();
  return ZX_OK;
}

zx_status_t Device::SetProps(fbl::Array<const zx_device_prop_t> props) {
  // This function should only be called once
  ZX_DEBUG_ASSERT(props_.data() == nullptr);

  props_ = std::move(props);

  return ZX_OK;
}

zx_status_t Device::SetStrProps(fbl::Array<const StrProperty> str_props) {
  // This function should only be called once.
  ZX_DEBUG_ASSERT(!str_props_.data());
  str_props_ = std::move(str_props);

  // Ensure that the string properties are encoded in UTF-8 format.
  for (const auto& str_prop : str_props_) {
    if (!fxl::IsStringUTF8(str_prop.key)) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (str_prop.value.valueless_by_exception()) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (str_prop.value.index() == StrPropValueType::String) {
      const auto str_value = std::get<StrPropValueType::String>(str_prop.value);
      if (!fxl::IsStringUTF8(str_value)) {
        return ZX_ERR_INVALID_ARGS;
      }
    }

    if (str_prop.value.index() == StrPropValueType::Enum) {
      const auto enum_value = std::get<StrPropValueType::Enum>(str_prop.value);
      if (!fxl::IsStringUTF8(enum_value)) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  return ZX_OK;
}

void Device::set_host(fbl::RefPtr<DriverHost> host) {
  if (host_) {
    host_->devices().erase(*this);
  }
  host_ = std::move(host);
  set_local_id(0);
  if (host_) {
    host_->devices().push_back(this);
    set_local_id(host_->new_device_id());
  }
}

// TODO(fxb/74654): Implement support for string properties.
void Device::AddDevice(AddDeviceRequestView request, AddDeviceCompleter::Sync& completer) {
  auto parent = fbl::RefPtr(this);
  std::string_view name(request->name.data(), request->name.size());
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());
  std::string_view args(request->args.data(), request->args.size());

  bool skip_autobind = static_cast<bool>(
      request->device_add_config & fuchsia_device_manager::wire::AddDeviceConfig::kSkipAutobind);

  fbl::RefPtr<Device> device;
  zx_status_t status = parent->coordinator->device_manager()->AddDevice(
      parent, std::move(request->device_controller), std::move(request->coordinator),
      request->property_list.props.data(), request->property_list.props.count(),
      request->property_list.str_props.data(), request->property_list.str_props.count(), name,
      request->protocol_id, driver_path, args, skip_autobind, request->has_init, kEnableAlwaysInit,
      std::move(request->inspect), std::move(request->client_remote),
      std::move(request->outgoing_dir), &device);
  if (device != nullptr && (request->device_add_config &
                            fuchsia_device_manager::wire::AddDeviceConfig::kAllowMultiComposite)) {
    device->flags |= DEV_CTX_ALLOW_MULTI_COMPOSITE;
  }
  uint64_t local_id = device != nullptr ? device->local_id() : 0;
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(local_id);
  }
}

void Device::ScheduleRemove(ScheduleRemoveRequestView request,
                            ScheduleRemoveCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);

  VLOGF(1, "Scheduling remove of device %p '%s'", dev.get(), dev->name().data());

  dev->coordinator->device_manager()->ScheduleDriverHostRequestedRemove(dev, request->unbind_self);
}

void Device::ScheduleUnbindChildren(ScheduleUnbindChildrenRequestView request,
                                    ScheduleUnbindChildrenCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);

  VLOGF(1, "Scheduling unbind of children for device %p '%s'", dev.get(), dev->name().data());

  dev->coordinator->device_manager()->ScheduleDriverHostRequestedUnbindChildren(dev);
}

void Device::BindDevice(BindDeviceRequestView request, BindDeviceCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());

  if (dev->coordinator->suspend_resume_manager()->InSuspend()) {
    LOGF(ERROR, "'bind-device' is forbidden in suspend");
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  VLOGF(1, "'bind-device' device %p '%s'", dev.get(), dev->name().data());
  zx_status_t status =
      dev->coordinator->bind_driver_manager()->BindDevice(dev, driver_path, false /* new device */);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Device::GetTopologicalPath(GetTopologicalPathRequestView request,
                                GetTopologicalPathCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  char path[fuchsia_device_manager::wire::kDevicePathMax + 1];
  zx_status_t status;
  if ((status = dev->coordinator->GetTopologicalPath(dev, path, sizeof(path))) != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  auto path_view = ::fidl::StringView::FromExternal(path);
  completer.ReplySuccess(path_view);
}

void Device::LoadFirmware(LoadFirmwareRequestView request, LoadFirmwareCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);

  char driver_path[fuchsia_device_manager::wire::kDevicePathMax + 1];
  memcpy(driver_path, request->driver_path.data(), request->driver_path.size());
  driver_path[request->driver_path.size()] = 0;

  char fw_path[fuchsia_device_manager::wire::kDevicePathMax + 1];
  memcpy(fw_path, request->fw_path.data(), request->fw_path.size());
  fw_path[request->fw_path.size()] = 0;

  dev->coordinator->firmware_loader()->LoadFirmware(
      dev, driver_path, fw_path,
      [completer = completer.ToAsync()](zx::status<LoadFirmwareResult> result) mutable {
        if (result.is_error()) {
          completer.ReplyError(result.status_value());
          return;
        }
        completer.ReplySuccess(std::move(result->vmo), result->size);
      });
}

void Device::GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  uint8_t data[fuchsia_device_manager::wire::kMetadataBytesMax];
  size_t actual = 0;
  zx_status_t status =
      dev->coordinator->GetMetadata(dev, request->key, data, sizeof(data), &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  auto data_view = ::fidl::VectorView<uint8_t>::FromExternal(data, actual);
  completer.ReplySuccess(data_view);
}

void Device::GetMetadataSize(GetMetadataSizeRequestView request,
                             GetMetadataSizeCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  size_t size;
  zx_status_t status = dev->coordinator->GetMetadataSize(dev, request->key, &size);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(size);
}

void Device::AddMetadata(AddMetadataRequestView request, AddMetadataCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  zx_status_t status = dev->coordinator->AddMetadata(dev, request->key, request->data.data(),
                                                     static_cast<uint32_t>(request->data.count()));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Device::AddCompositeDevice(AddCompositeDeviceRequestView request,
                                AddCompositeDeviceCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  std::string_view name(request->name.data(), request->name.size());
  zx_status_t status = this->coordinator->device_manager()->AddCompositeDevice(
      dev, name, std::move(request->comp_desc));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Device::AddDeviceGroup(AddDeviceGroupRequestView request,
                            AddDeviceGroupCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  zx_status_t status =
      this->coordinator->AddDeviceGroup(dev, request->name.get(), std::move(request->group_desc));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

bool Device::DriverLivesInSystemStorage() const {
  const std::string kSystemPrefix = "/system/";

  if (libname().size() < kSystemPrefix.size()) {
    return false;
  }
  return (kSystemPrefix.compare(0, kSystemPrefix.size() - 1, libname().c_str(),
                                kSystemPrefix.size() - 1) == 0);
}

bool Device::IsAlreadyBound() const {
  return (flags & DEV_CTX_BOUND) && !(flags & DEV_CTX_ALLOW_MULTI_COMPOSITE) &&
         !(flags & DEV_CTX_MULTI_BIND);
}
