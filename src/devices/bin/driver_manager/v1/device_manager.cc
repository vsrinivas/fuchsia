// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/device_manager.h"

#include <lib/ddk/driver.h>

#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/lib/log/log.h"

namespace fdm = fuchsia_device_manager;

DeviceManager::DeviceManager(Coordinator* coordinator, DriverHostCrashPolicy crash_policy)
    : coordinator_(coordinator), crash_policy_(crash_policy) {}

zx_status_t DeviceManager::AddDevice(
    const fbl::RefPtr<Device>& parent, fidl::ClientEnd<fdm::DeviceController> device_controller,
    fidl::ServerEnd<fdm::Coordinator> coordinator, const fdm::wire::DeviceProperty* props_data,
    size_t props_count, const fdm::wire::DeviceStrProperty* str_props_data, size_t str_props_count,
    std::string_view name, uint32_t protocol_id, std::string_view driver_path,
    std::string_view args, bool skip_autobind, bool has_init, bool always_init, zx::vmo inspect,
    zx::channel client_remote, fidl::ClientEnd<fio::Directory> outgoing_dir,
    fbl::RefPtr<Device>* new_device) {
  // If this is true, then |name_data|'s size is properly bounded.
  static_assert(fdm::wire::kDeviceNameMax == ZX_DEVICE_NAME_MAX);
  static_assert(fdm::wire::kPropertiesMax <= UINT32_MAX);

  if (coordinator_->suspend_resume_manager()->InSuspend()) {
    LOGF(ERROR, "Add device '%.*s' forbidden in suspend", static_cast<int>(name.size()),
         name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (coordinator_->suspend_resume_manager()->InResume()) {
    LOGF(ERROR, "Add device '%.*s' forbidden in resume", static_cast<int>(name.size()),
         name.data());
    return ZX_ERR_BAD_STATE;
  }

  if (parent->state() == Device::State::kUnbinding) {
    LOGF(ERROR, "Add device '%.*s' forbidden while parent is unbinding",
         static_cast<int>(name.size()), name.data());
    return ZX_ERR_BAD_STATE;
  }

  // Convert the device properties and string properties.
  fbl::Array<zx_device_prop_t> props(new zx_device_prop_t[props_count], props_count);
  if (!props) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < props_count; i++) {
    props[i] = zx_device_prop_t{
        .id = props_data[i].id,
        .reserved = props_data[i].reserved,
        .value = props_data[i].value,
    };
  }

  fbl::Array<StrProperty> str_props(new StrProperty[str_props_count], str_props_count);
  if (!str_props) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < str_props_count; i++) {
    str_props[i].key = str_props_data[i].key.get();
    if (str_props_data[i].value.is_int_value()) {
      str_props[i].value = str_props_data[i].value.int_value();
    } else if (str_props_data[i].value.is_str_value()) {
      str_props[i].value = std::string(str_props_data[i].value.str_value().get());
    } else if (str_props_data[i].value.is_bool_value()) {
      str_props[i].value = str_props_data[i].value.bool_value();
    }
  }

  fbl::String name_str(name);
  fbl::String driver_path_str(driver_path);
  fbl::String args_str(args);

  // TODO(fxbug.dev/43370): remove this check once init tasks can be enabled for all devices.
  bool want_init_task = has_init || always_init;
  fbl::RefPtr<Device> dev;
  zx_status_t status = Device::Create(
      coordinator_, parent, std::move(name_str), std::move(driver_path_str), std::move(args_str),
      protocol_id, std::move(props), std::move(str_props), std::move(coordinator),
      std::move(device_controller), want_init_task, skip_autobind, std::move(inspect),
      std::move(client_remote), std::move(outgoing_dir), &dev);
  if (status != ZX_OK) {
    return status;
  }

  devices_.push_back(dev);

  // Note that |dev->parent()| may not match |parent| here, so we should always
  // use |dev->parent()|.  This case can happen if |parent| refers to a device
  // proxy.

  // If we're creating a device that's using the fragment driver, inform the
  // fragment.
  if (dev->libname() == coordinator_->GetFragmentDriverUrl()) {
    for (auto& cur_fragment : dev->parent()->fragments()) {
      // Pick the first fragment that does not have a device added by the fragment
      // driver.
      if (cur_fragment.fragment_device() == nullptr &&
          !cur_fragment.bound_device()->has_outgoing_directory()) {
        cur_fragment.set_fragment_device(dev);
        status = cur_fragment.composite()->TryAssemble();
        if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
          LOGF(ERROR, "Failed to assemble composite device: %s", zx_status_get_string(status));
        }
        break;
      }
    }
  }

  VLOGF(1, "Added device %p '%s'", dev.get(), dev->name().data());
  // TODO(fxbug.dev/43370): remove this once init tasks can be enabled for all devices.
  if (!want_init_task) {
    status = dev->SignalReadyForBind();
    if (status != ZX_OK) {
      return status;
    }
    VLOGF(1, "Published device %p '%s' args='%s' props=%zu parent=%p", dev.get(),
          dev->name().data(), dev->args().data(), dev->props().size(), dev->parent().get());
  }

  *new_device = std::move(dev);
  return ZX_OK;
}

zx_status_t DeviceManager::AddCompositeDevice(const fbl::RefPtr<Device>& dev, std::string_view name,
                                              fdm::wire::CompositeDeviceDescriptor comp_desc) {
  std::unique_ptr<CompositeDevice> new_device;
  zx_status_t status = CompositeDevice::Create(name, std::move(comp_desc), &new_device);
  if (status != ZX_OK) {
    return status;
  }

  // Try to bind the new composite device specification against existing
  // devices.
  for (auto& dev : devices_) {
    if (!dev.is_bindable() && !dev.is_composite_bindable()) {
      continue;
    }

    auto dev_ref = fbl::RefPtr(&dev);
    size_t index;
    if (new_device->TryMatchFragments(dev_ref, &index)) {
      LOGF(INFO, "Device '%s' matched fragment %zu of composite '%s'", dev.name().data(), index,
           new_device->name().data());
      status = new_device->BindFragment(index, dev_ref);
      if (status != ZX_OK) {
        LOGF(ERROR, "Device '%s' failed to bind fragment %zu of composite '%s': %s",
             dev.name().data(), index, new_device->name().data(), zx_status_get_string(status));
      }
    }
  }

  composite_devices_.push_back(std::move(new_device));
  return ZX_OK;
}

void DeviceManager::AddToDevices(fbl::RefPtr<Device> new_device) { devices_.push_back(new_device); }

void DeviceManager::HandleNewDevice(const fbl::RefPtr<Device>& dev) {
  // If the device has a proxy, we actually want to wait for the proxy device to be
  // created and connect to that.
  if (!(dev->flags & DEV_CTX_MUST_ISOLATE)) {
    zx::channel client_remote = dev->take_client_remote();
    if (client_remote.is_valid()) {
      zx_status_t status =
          devfs_connect(dev.get(), fidl::ServerEnd<fio::Node>(std::move(client_remote)));
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to connect to service from proxy device '%s': %s", dev->name().data(),
             zx_status_get_string(status));
      }
    }
  }

  // TODO(tesienbe): We probably should do something with the return value
  // from this...
  coordinator_->bind_driver_manager()->BindDevice(dev, {} /* libdrvname */, true /* new device */);
}

void DeviceManager::ScheduleRemove(const fbl::RefPtr<Device>& dev) {
  dev->CreateUnbindRemoveTasks(
      UnbindTaskOpts{.do_unbind = false, .post_on_create = true, .driver_host_requested = false});
}

void DeviceManager::ScheduleDriverHostRequestedRemove(const fbl::RefPtr<Device>& dev,
                                                      bool do_unbind) {
  dev->CreateUnbindRemoveTasks(UnbindTaskOpts{
      .do_unbind = do_unbind, .post_on_create = true, .driver_host_requested = true});
}

void DeviceManager::ScheduleDriverHostRequestedUnbindChildren(const fbl::RefPtr<Device>& parent) {
  for (auto& child : parent->children()) {
    child.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = true, .driver_host_requested = true});
  }
}

void DeviceManager::ScheduleUnbindRemoveAllDevices(const fbl::RefPtr<DriverHost> driver_host) {
  for (auto& dev : driver_host->devices()) {
    // This will also call on all the children of the device.
    dev.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = true, .driver_host_requested = false});
  }
}

zx_status_t DeviceManager::RemoveDevice(const fbl::RefPtr<Device>& dev, bool forced) {
  if (forced && crash_policy_ == DriverHostCrashPolicy::kRebootSystem) {
    // TODO(fxbug.dev/67168): Trigger system restart more gracefully.
    ZX_ASSERT(false);
  }
  dev->inc_num_removal_attempts();

  if (dev->state() == Device::State::kDead) {
    // This should not happen
    LOGF(ERROR, "Cannot remove device %p '%s' twice", dev.get(), dev->name().data());
    return ZX_ERR_BAD_STATE;
  }
  if (dev->flags & DEV_CTX_IMMORTAL) {
    // This too should not happen
    LOGF(ERROR, "Cannot remove device %p '%s' (immortal)", dev.get(), dev->name().data());
    return ZX_ERR_BAD_STATE;
  }

  LOGF(INFO, "Removing device %p '%s' parent=%p", dev.get(), dev->name().data(),
       dev->parent().get());
  dev->set_state(Device::State::kDead);

  // remove from devfs, preventing further OPEN attempts
  devfs_unpublish(dev.get());

  // Mark any suspend that's in-flight as completed, since if the device is
  // removed it should be in its lowest state.
  // TODO(teisenbe): Should we mark it as failed if this is a forced removal?
  dev->CompleteSuspend(ZX_OK);
  dev->CompleteInit(ZX_ERR_UNAVAILABLE);

  fbl::RefPtr<DriverHost> dh = dev->host();
  bool driver_host_dying = (dh != nullptr && (dh->flags() & DriverHost::Flags::kDying));
  if (forced || driver_host_dying) {
    // We are force removing all devices in the driver_host, so force complete any outstanding
    // tasks.
    dev->CompleteUnbind(ZX_ERR_UNAVAILABLE);
    dev->CompleteRemove(ZX_ERR_UNAVAILABLE);

    // If there is a device proxy, we need to create a new unbind task for it.
    // For non-forced removals, the unbind task will handle scheduling the proxy removal.
    if (dev->proxy()) {
      ScheduleRemove(dev->proxy());
    }
    if (dev->new_proxy()) {
      ScheduleRemove(dev->new_proxy());
    }
  } else {
    // We should not be removing a device while the unbind task is still running.
    ZX_ASSERT(dev->GetActiveUnbind() == nullptr);
  }

  // Check if this device is a composite device, and if so disconnects from it
  if (dev->composite()) {
    dev->composite()->Remove();
  }

  // Check if this device is a composite fragment device
  if (dev->libname() == coordinator_->GetFragmentDriverUrl()) {
    // If it is, then its parent will know about which one (since the parent
    // is the actual device matched by the fragment description).
    const auto& parent = dev->parent();

    for (auto itr = parent->fragments().begin(); itr != parent->fragments().end();) {
      auto& cur_fragment = *itr;
      // Advance the iterator because we will erase the current element from the list.
      ++itr;
      if (cur_fragment.fragment_device() == dev) {
        cur_fragment.Unbind();
        parent->fragments().erase(cur_fragment);
        break;
      }
    }
  }

  // Detach from driver_host
  if (dh != nullptr) {
    // We're holding on to a reference to the driver_host through |dh|.
    // This is necessary to prevent it from being freed in the middle of
    // the code below.
    dev->set_host(nullptr);

    // If we are responding to a disconnect, we'll remove all the other devices
    // on this driver_host too. A side-effect of this is that the driver_host
    // will be released, as well as any proxy devices.
    if (forced) {
      dh->flags() |= DriverHost::Flags::kDying;

      fbl::RefPtr<Device> next;
      fbl::RefPtr<Device> last;
      while (!dh->devices().is_empty()) {
        next = fbl::RefPtr(&dh->devices().front());
        if (last == next) {
          // This shouldn't be possible, but let's not infinite-loop if it happens
          LOGF(FATAL, "Failed to remove device %p '%s' from driver_host", next.get(),
               next->name().data());
        }
        RemoveDevice(next, false);
        last = std::move(next);
      }

      // TODO: set a timer so if this driver_host does not finish dying
      //      in a reasonable amount of time, we fix the glitch.
    }

    dh.reset();
  }

  // if we have a parent, disconnect and downref it
  fbl::RefPtr<Device> parent = dev->parent();
  if (parent != nullptr) {
    Device* real_parent;
    if (parent->flags & DEV_CTX_PROXY) {
      real_parent = parent->parent().get();
    } else {
      real_parent = parent.get();
    }
    dev->DetachFromParent();
    if (!(dev->flags & DEV_CTX_PROXY)) {
      if (parent->children().is_empty()) {
        parent->flags &= (~DEV_CTX_BOUND);
        if (real_parent->test_state() == Device::TestStateMachine::kTestUnbindSent) {
          real_parent->test_event().signal(0, TEST_REMOVE_DONE_SIGNAL);
          if (!(dev->flags & DEV_CTX_PROXY)) {
            // remove from list of all devices
            devices_.erase(*dev);
          }
          return ZX_OK;
        }

        // TODO: This code is to cause the bind process to
        //      restart and get a new driver_host to be launched
        //      when a driver_host dies.  It should probably be
        //      more tied to driver_host teardown than it is.
        // IF the policy is set such that we take action
        // AND we are the last child of our parent
        // AND our parent is not itself dead
        // AND our parent is a BUSDEV
        // AND our parent's driver_host is not dying
        // THEN we will want to rebind our parent
        if (crash_policy_ == DriverHostCrashPolicy::kRestartDriverHost &&
            parent->state() != Device::State::kDead && parent->flags & DEV_CTX_MUST_ISOLATE &&
            ((parent->host() == nullptr) ||
             !(parent->host()->flags() & DriverHost::Flags::kDying))) {
          VLOGF(1, "Bus device %p '%s' is unbound", parent.get(), parent->name().data());

          if (parent->retries > 0) {
            LOGF(INFO, "Suspected crash: attempting to re-bind %s", parent->name().data());
            // Add device with an exponential backoff.
            zx_status_t r = parent->SignalReadyForBind(parent->backoff);
            if (r != ZX_OK) {
              return r;
            }
            parent->backoff *= 2;
            parent->retries--;
          }
        }
      }
    }
  }

  if (!(dev->flags & DEV_CTX_PROXY)) {
    // remove from list of all devices
    devices_.erase(*dev);
  }

  return ZX_OK;
}
