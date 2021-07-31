// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/driver/test/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/coding.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <string_view>

#include "coordinator.h"
#include "devfs.h"
#include "fidl_txn.h"
#include "init_task.h"
#include "resume_task.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/utf_codecs.h"
#include "suspend_task.h"

// TODO(fxbug.dev/43370): remove this once init tasks can be enabled for all devices.
static constexpr bool kEnableAlwaysInit = false;

Device::Device(Coordinator* coord, fbl::String name, fbl::String libname, fbl::String args,
               fbl::RefPtr<Device> parent, uint32_t protocol_id, zx::vmo inspect_vmo,
               zx::channel client_remote)
    : coordinator(coord),
      name_(std::move(name)),
      libname_(std::move(libname)),
      args_(std::move(args)),
      parent_(std::move(parent)),
      protocol_id_(protocol_id),
      publish_task_([this] { coordinator->HandleNewDevice(fbl::RefPtr(this)); }),
      client_remote_(std::move(client_remote)) {
  test_reporter = std::make_unique<DriverTestReporter>(name_);
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

  std::unique_ptr<Metadata> md;
  while ((md = metadata_.pop_front()) != nullptr) {
    if (md->has_path) {
      // return to published_metadata list
      coordinator->AppendPublishedMetadata(std::move(md));
    } else {
      // metadata was attached directly to this device, so we release it now
    }
  }

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
        switch (info.reason()) {
          case fidl::Reason::kUnbind:
          case fidl::Reason::kClose:
            // These are initiated by ourself.
            break;
          case fidl::Reason::kPeerClosed:
            // If the device is already dead, we are detecting an expected disconnect from the
            // driver_host.
            if (dev->state() != Device::State::kDead) {
              // TODO(https://fxbug.dev/56208): Change this log back to error once isolated devmgr
              // is fixed.
              LOGF(WARNING, "Disconnected device %p '%s', see fxbug.dev/56208 for potential cause",
                   dev.get(), dev->name().data());

              dev->coordinator->RemoveDevice(dev, true);
            }
            break;
          default:
            LOGF(ERROR, "Failed to handle RPC for device %p '%s': %s", dev.get(),
                 dev->name().data(), info.FormatDescription().c_str());
        }
      });
}

zx_status_t Device::Create(
    Coordinator* coordinator, const fbl::RefPtr<Device>& parent, fbl::String name,
    fbl::String driver_path, fbl::String args, uint32_t protocol_id,
    fbl::Array<zx_device_prop_t> props, fbl::Array<StrProperty> str_props,
    fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
    fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
    bool want_init_task, bool skip_autobind, zx::vmo inspect, zx::channel client_remote,
    fbl::RefPtr<Device>* device) {
  fbl::RefPtr<Device> real_parent;
  // If our parent is a proxy, for the purpose of devfs, we need to work with
  // *its* parent which is the device that it is proxying.
  if (parent->flags & DEV_CTX_PROXY) {
    real_parent = parent->parent();
  } else {
    real_parent = parent;
  }

  auto dev = fbl::MakeRefCounted<Device>(coordinator, std::move(name), std::move(driver_path),
                                         std::move(args), real_parent, protocol_id,
                                         std::move(inspect), std::move(client_remote));
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
                                         fbl::String(), nullptr, 0, zx::vmo(), zx::channel());
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

  auto dev =
      fbl::MakeRefCounted<Device>(this->coordinator, name_, std::move(driver_path), fbl::String(),
                                  fbl::RefPtr(this), protocol_id_, zx::vmo(), zx::channel());
  if (dev == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  dev->flags = DEV_CTX_PROXY;

  dev->InitializeInspectValues();

  proxy_ = std::move(dev);
  VLOGF(1, "Created proxy device %p '%s'", this, name_.data());
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

zx_status_t Device::SendInit(InitCompletion completion) {
  ZX_ASSERT(!init_completion_);

  VLOGF(1, "Initializing device %p '%s'", this, name_.data());
  auto result = device_controller()->Init([dev = fbl::RefPtr(this)](auto* response) {
    VLOGF(1, "Initialized device %p '%s': %s", dev.get(), dev->name().data(),
          zx_status_get_string(response->status));
    dev->CompleteInit(response->status);
  });
  if (!result.ok()) {
    return result.status();
  }
  init_completion_ = std::move(completion);
  return ZX_OK;
}

zx_status_t Device::CompleteInit(zx_status_t status) {
  if (!init_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when initializing device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  if (init_completion_) {
    init_completion_(status);
  }
  active_init_ = nullptr;
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

zx_status_t Device::SendSuspend(uint32_t flags, SuspendCompletion completion) {
  if (suspend_completion_) {
    // We already have a pending suspend
    return ZX_ERR_UNAVAILABLE;
  }
  VLOGF(1, "Suspending device %p '%s'", this, name_.data());
  auto result = device_controller()->Suspend(flags, [dev = fbl::RefPtr(this)](auto* response) {
    if (response->status == ZX_OK) {
      LOGF(DEBUG, "Suspended device %p '%s'successfully", dev.get(), dev->name().data());
    } else {
      LOGF(ERROR, "Failed to suspended device %p '%s': %s", dev.get(), dev->name().data(),
           zx_status_get_string(response->status));
    }
    dev->CompleteSuspend(response->status);
  });
  if (!result.ok()) {
    return result.status();
  }
  set_state(Device::State::kSuspending);
  suspend_completion_ = std::move(completion);
  return ZX_OK;
}

zx_status_t Device::SendResume(uint32_t target_system_state, ResumeCompletion completion) {
  if (resume_completion_) {
    // We already have a pending resume
    return ZX_ERR_UNAVAILABLE;
  }
  VLOGF(1, "Resuming device %p '%s'", this, name_.data());

  auto result =
      device_controller()->Resume(target_system_state, [dev = fbl::RefPtr(this)](auto* response) {
        LOGF(INFO, "Resumed device %p '%s': %s", dev.get(), dev->name().data(),
             zx_status_get_string(response->status));
        dev->CompleteResume(response->status);
      });
  if (!result.ok()) {
    return result.status();
  }
  set_state(Device::State::kResuming);
  resume_completion_ = std::move(completion);
  return ZX_OK;
}

void Device::CompleteSuspend(zx_status_t status) {
  if (status == ZX_OK) {
    // If a device is being removed, any existing suspend task will be forcibly completed,
    // in which case we should not update the state.
    if (state_ != Device::State::kDead) {
      set_state(Device::State::kSuspended);
    }
  } else {
    set_state(Device::State::kActive);
  }

  active_suspend_ = nullptr;
  if (suspend_completion_) {
    suspend_completion_(status);
  }
}

void Device::CompleteResume(zx_status_t status) {
  if (status != ZX_OK) {
    set_state(Device::State::kSuspended);
  } else {
    set_state(Device::State::kResumed);
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

zx_status_t Device::SendUnbind(UnbindCompletion& completion) {
  if (unbind_completion_) {
    // We already have a pending unbind
    return ZX_ERR_UNAVAILABLE;
  }
  VLOGF(1, "Unbinding device %p '%s'", this, name_.data());
  set_state(Device::State::kUnbinding);
  auto result = device_controller()->Unbind([dev = fbl::RefPtr(this)](auto* response) {
    LOGF(INFO, "Unbound device %p '%s': %s", dev.get(), dev->name().data(),
         zx_status_get_string(response->result.is_err() ? response->result.err() : ZX_OK));
    dev->CompleteUnbind();
  });
  if (!result.ok()) {
    return result.status();
  }
  // Only take ownership if sending succeeded, as otherwise the caller
  // will want to handle calling the completion.
  unbind_completion_ = std::move(completion);
  return ZX_OK;
}

zx_status_t Device::SendCompleteRemove(RemoveCompletion& completion) {
  if (remove_completion_) {
    // We already have a pending remove.
    return ZX_ERR_UNAVAILABLE;
  }
  VLOGF(1, "Completing removal of device %p '%s'", this, name_.data());
  set_state(Device::State::kUnbinding);
  auto result = device_controller()->CompleteRemoval([dev = fbl::RefPtr(this)](auto* response) {
    LOGF(INFO, "Removed device %p '%s': %s", dev.get(), dev->name().data(),
         zx_status_get_string(response->result.is_err() ? response->result.err() : ZX_OK));
    dev->CompleteRemove();
  });
  if (!result.ok()) {
    return result.status();
  }
  // Only take ownership if sending succeeded, as otherwise the caller
  // will want to handle calling the completion.
  remove_completion_ = std::move(completion);
  return ZX_OK;
}

zx_status_t Device::CompleteUnbind(zx_status_t status) {
  if (!unbind_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when unbinding device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  if (unbind_completion_) {
    unbind_completion_(status);
  }
  active_unbind_ = nullptr;
  return ZX_OK;
}

zx_status_t Device::CompleteRemove(zx_status_t status) {
  if (!remove_completion_ && status == ZX_OK) {
    LOGF(ERROR, "Unexpected reply when removing device %p '%s'", this, name_.data());
    return ZX_ERR_IO;
  }
  // If we received an error, it is because we are currently force removing the device.
  if (status == ZX_OK) {
    coordinator->RemoveDevice(fbl::RefPtr(this), false);
  }
  if (remove_completion_) {
    // If we received an error, it is because we are currently force removing the device.
    // In that case, all other devices in the driver_host will be force removed too,
    // and they will call CompleteRemove() before the remove task is scheduled to run.
    // For ancestor dependents in other driver_hosts, we want them to proceed removal as usual.
    remove_completion_(ZX_OK);
  }
  active_remove_ = nullptr;
  return ZX_OK;
}

void Device::HandleTestOutput(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to wait on test output for device %p '%s': %s", this, name_.data(),
         zx_status_get_string(status));
    return;
  }
  if (!(signal->observed & ZX_CHANNEL_PEER_CLOSED)) {
    LOGF(WARNING, "Unexpected signal state %#08x for device %p '%s'", signal->observed, this,
         name_.data());
    return;
  }

  test_reporter->TestStart();

  fidl::BindServer(dispatcher,
                   fidl::ServerEnd<fuchsia_driver_test::Logger>(std::move(test_output_)),
                   test_reporter.get(),
                   [this](DriverTestReporter* test_reporter, fidl::UnbindInfo info,
                          fidl::ServerEnd<fuchsia_driver_test::Logger> server_end) {
                     switch (info.reason()) {
                       case fidl::Reason::kPeerClosed:
                         test_reporter->TestFinished();
                         break;
                       default: {
                         auto reason = info.FormatDescription();
                         LOGF(ERROR, "Unexpected server error for device %p '%s': %s", this,
                              name_.data(), reason.c_str());
                       }
                     }
                   });
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
  for (const auto str_prop : str_props_) {
    if (!fxl::IsStringUTF8(str_prop.key)) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (str_prop.value.valueless_by_exception()) {
      return ZX_ERR_INVALID_ARGS;
    }

    auto* str_value = std::get_if<std::string>(&str_prop.value);
    if (str_value && !fxl::IsStringUTF8(*str_value)) {
      return ZX_ERR_INVALID_ARGS;
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

const char* Device::GetTestDriverName() {
  for (auto& child : children()) {
    return this->coordinator->LibnameToDriver(child.libname().data())->name.data();
  }
  return nullptr;
}

void Device::DriverCompatibilityTest(
    zx::duration test_time, std::optional<RunCompatibilityTestsCompleter::Async> completer) {
  thrd_t t;
  if (test_state() != TestStateMachine::kTestNotStarted) {
    if (completer) {
      completer->ReplySuccess(fuchsia_device_manager::wire::CompatibilityTestStatus::kErrInternal);
    }
    return;
  }
  set_test_time(test_time);
  if (completer) {
    compatibility_test_completer_ = *std::move(completer);
  }
  auto cb = [](void* arg) -> int {
    auto dev = fbl::RefPtr(reinterpret_cast<Device*>(arg));
    return dev->RunCompatibilityTests();
  };
  int ret = thrd_create_with_name(&t, cb, this, "compatibility-tests-thread");
  if (ret != thrd_success) {
    LOGF(ERROR, "Failed to create thread for driver compatibility test '%s': %d",
         GetTestDriverName(), ret);
    if (compatibility_test_completer_) {
      compatibility_test_completer_->ReplySuccess(
          fuchsia_device_manager::wire::CompatibilityTestStatus::kErrInternal);
      compatibility_test_completer_.reset();
    }
    return;
  }
  thrd_detach(t);
}

#define TEST_LOGF(severity, message...) FX_LOGF(severity, "compatibility", message)

int Device::RunCompatibilityTests() {
  const char* test_driver_name = GetTestDriverName();
  TEST_LOGF(INFO, "Running test '%s'", test_driver_name);
  auto cleanup = fit::defer([this]() {
    if (compatibility_test_completer_) {
      compatibility_test_completer_->ReplySuccess(test_status_);
    }
    test_event().reset();
    set_test_state(Device::TestStateMachine::kTestDone);
    compatibility_test_completer_.reset();
  });
  // Device should be bound for test to work
  if (!(flags & DEV_CTX_BOUND) || children().is_empty()) {
    TEST_LOGF(ERROR, "[  FAILED  ] %s: Parent device not bound", test_driver_name);
    test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrBindNoDdkadd;
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = zx::event::create(0, &test_event());
  if (status != ZX_OK) {
    TEST_LOGF(ERROR, "[  FAILED  ] %s: Event creation failed, %s", test_driver_name,
              zx_status_get_string(status));
    test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrInternal;
    return ZX_ERR_INTERNAL;
  }

  // Issue unbind on all its children.
  for (auto itr = children().begin(); itr != children().end();) {
    auto& child = *itr;
    itr++;
    this->set_test_state(Device::TestStateMachine::kTestUnbindSent);
    coordinator->ScheduleDriverHostRequestedRemove(fbl::RefPtr<Device>(&child),
                                                   true /* unbind_self */);
  }

  zx_signals_t observed = 0;
  // Now wait for the device to be removed.
  status =
      test_event().wait_one(TEST_REMOVE_DONE_SIGNAL, zx::deadline_after(test_time()), &observed);
  if (status != ZX_OK) {
    if (status == ZX_ERR_TIMED_OUT) {
      // The Remove did not complete.
      TEST_LOGF(ERROR,
                "[  FAILED  ] %s: Timed out waiting for device to be removed, check if"
                " device_remove() was called in the unbind routine of the driver: %s",
                test_driver_name, zx_status_get_string(status));
      test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrUnbindTimeout;
    } else {
      TEST_LOGF(ERROR, "[  FAILED  ] %s: Error waiting for device to be removed: %s",
                test_driver_name, zx_status_get_string(status));
      test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrInternal;
    }
    return ZX_ERR_INTERNAL;
  }
  this->set_test_state(Device::TestStateMachine::kTestBindSent);
  this->coordinator->HandleNewDevice(fbl::RefPtr(this));
  observed = 0;
  status = test_event().wait_one(TEST_BIND_DONE_SIGNAL, zx::deadline_after(test_time()), &observed);
  if (status != ZX_OK) {
    if (status == ZX_ERR_TIMED_OUT) {
      // The Bind did not complete.
      TEST_LOGF(ERROR,
                "[  FAILED  ] %s: Timed out waiting for driver to be bound, check if there"
                " is blocking IO in the driver's bind(): %s",
                test_driver_name, zx_status_get_string(status));
      test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrBindTimeout;
    } else {
      TEST_LOGF(ERROR, "[  FAILED  ] %s: Error waiting for driver to be bound: %s",
                test_driver_name, zx_status_get_string(status));
      test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrInternal;
    }
    return ZX_ERR_INTERNAL;
  }
  this->set_test_state(Device::TestStateMachine::kTestBindDone);
  if (this->children().is_empty()) {
    TEST_LOGF(ERROR,
              "[  FAILED  ] %s: Driver did not add a child device in bind(), check if it called "
              "DdkAdd()",
              test_driver_name);
    test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kErrBindNoDdkadd;
    return -1;
  }
  TEST_LOGF(INFO, "[  PASSED  ] %s", test_driver_name);
  // TODO(ravoorir): Test Suspend and Resume hooks
  test_status_ = fuchsia_device_manager::wire::CompatibilityTestStatus::kOk;
  return ZX_OK;
}

#undef TEST_LOGF

// TODO(fxb/74654): Implement support for string properties.
void Device::AddDevice(AddDeviceRequestView request, AddDeviceCompleter::Sync& completer) {
  auto parent = fbl::RefPtr(this);
  std::string_view name(request->name.data(), request->name.size());
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());
  std::string_view args(request->args.data(), request->args.size());

  bool skip_autobind = static_cast<bool>(
      request->device_add_config & fuchsia_device_manager::wire::AddDeviceConfig::kSkipAutobind);

  fbl::RefPtr<Device> device;
  zx_status_t status = parent->coordinator->AddDevice(
      parent, std::move(request->device_controller), std::move(request->coordinator),
      request->property_list.props.data(), request->property_list.props.count(),
      request->property_list.str_props.data(), request->property_list.str_props.count(), name,
      request->protocol_id, driver_path, args, skip_autobind, request->has_init, kEnableAlwaysInit,
      std::move(request->inspect), std::move(request->client_remote), &device);
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

void Device::PublishMetadata(PublishMetadataRequestView request,
                             PublishMetadataCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  char path[fuchsia_device_manager::wire::kDevicePathMax + 1];
  ZX_ASSERT(request->device_path.size() <= fuchsia_device_manager::wire::kDevicePathMax);
  memcpy(path, request->device_path.data(), request->device_path.size());
  path[request->device_path.size()] = 0;
  zx_status_t status = dev->coordinator->PublishMetadata(
      dev, path, request->key, request->data.data(), static_cast<uint32_t>(request->data.count()));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Device::ScheduleRemove(ScheduleRemoveRequestView request,
                            ScheduleRemoveCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);

  VLOGF(1, "Scheduling remove of device %p '%s'", dev.get(), dev->name().data());

  dev->coordinator->ScheduleDriverHostRequestedRemove(dev, request->unbind_self);
}

void Device::ScheduleUnbindChildren(ScheduleUnbindChildrenRequestView request,
                                    ScheduleUnbindChildrenCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);

  VLOGF(1, "Scheduling unbind of children for device %p '%s'", dev.get(), dev->name().data());

  dev->coordinator->ScheduleDriverHostRequestedUnbindChildren(dev);
}

void Device::BindDevice(BindDeviceRequestView request, BindDeviceCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  std::string_view driver_path(request->driver_path.data(), request->driver_path.size());

  if (dev->coordinator->InSuspend()) {
    LOGF(ERROR, "'bind-device' is forbidden in suspend");
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  VLOGF(1, "'bind-device' device %p '%s'", dev.get(), dev->name().data());
  zx_status_t status = dev->coordinator->BindDevice(dev, driver_path, false /* new device */);
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

  zx::vmo vmo;
  uint64_t size = 0;
  zx_status_t status;
  if ((status = dev->coordinator->LoadFirmware(dev, driver_path, fw_path, &vmo, &size)) != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  completer.ReplySuccess(std::move(vmo), size);
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
void Device::RunCompatibilityTests(RunCompatibilityTestsRequestView request,
                                   RunCompatibilityTestsCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  fbl::RefPtr<Device>& real_parent = dev;
  if (dev->flags & DEV_CTX_PROXY) {
    real_parent = dev->parent();
  }
  zx::duration test_time = zx::nsec(request->hook_wait_time);
  real_parent->DriverCompatibilityTest(test_time, completer.ToAsync());
}

void Device::AddCompositeDevice(AddCompositeDeviceRequestView request,
                                AddCompositeDeviceCompleter::Sync& completer) {
  auto dev = fbl::RefPtr(this);
  std::string_view name(request->name.data(), request->name.size());
  zx_status_t status =
      this->coordinator->AddCompositeDevice(dev, name, std::move(request->comp_desc));
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
