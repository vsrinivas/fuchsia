// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/inspect.h"

#include <lib/ddk/driver.h>
#include <lib/inspect/service/cpp/service.h>

#include <utility>

#include <driver-info/driver-info.h>

#include "src/devices/bin/driver_manager/device.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

zx::status<> InspectDevfs::Publish(const fbl::RefPtr<Device>& dev) { return AddClassDirEntry(dev); }

// TODO(surajmalhotra): Ideally this would take a RefPtr, but currently this is
// invoked in the dtor for Device.
void InspectDevfs::Unpublish(Device* dev) {
  // Remove reference in class directory if it exists
  auto [dir, seqcount] = GetProtoDir(dev->protocol_id());
  if (dir == nullptr) {
    // No class dir for this type, so ignore it
    return;
  }
  std::optional file_opt = dev->inspect().file();
  if (!file_opt.has_value()) {
    // No inspect file for this device.
    return;
  }
  fbl::RefPtr file = file_opt.value();
  dir->RemoveEntry(dev->link_name(), file.get());
  // Keep only those protocol directories which are not empty to avoid clutter
  RemoveEmptyProtoDir(dev->protocol_id());
}

InspectDevfs::InspectDevfs(fbl::RefPtr<fs::PseudoDir> root_dir,
                           fbl::RefPtr<fs::PseudoDir> class_dir)
    : root_dir_(std::move(root_dir)), class_dir_(std::move(class_dir)) {
  std::copy(std::begin(kProtoInfos), std::end(kProtoInfos), proto_infos_.begin());
}

zx::status<InspectDevfs> InspectDevfs::Create(const fbl::RefPtr<fs::PseudoDir>& root_dir) {
  auto class_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  zx::status<> status = zx::make_status(root_dir->AddEntry("class", class_dir));
  if (status.is_error()) {
    return status.take_error();
  }

  InspectDevfs devfs(root_dir, class_dir);

  return zx::ok(std::move(devfs));
}

std::tuple<fbl::RefPtr<fs::PseudoDir>, uint32_t*> InspectDevfs::GetProtoDir(uint32_t id) {
  for (auto& info : proto_infos_) {
    if (info.id == id) {
      return {info.devnode, &info.seqcount};
    }
  }
  return {nullptr, nullptr};
}

std::tuple<fbl::RefPtr<fs::PseudoDir>, uint32_t*> InspectDevfs::GetOrCreateProtoDir(uint32_t id) {
  for (auto& info : proto_infos_) {
    if (info.id == id) {
      // Create protocol directory if one doesn't exist
      if (!info.devnode) {
        auto node = fbl::MakeRefCounted<fs::PseudoDir>();
        if (class_dir_->AddEntry(info.name, node) != ZX_OK) {
          return {nullptr, nullptr};
        }
        info.devnode = std::move(node);
      }
      return {info.devnode, &info.seqcount};
    }
  }
  return {nullptr, nullptr};
}

void InspectDevfs::RemoveEmptyProtoDir(uint32_t id) {
  for (auto& info : proto_infos_) {
    if (info.id == id && info.devnode && info.devnode->IsEmpty()) {
      class_dir_->RemoveEntry(info.name, info.devnode.get());
      info.devnode = nullptr;
    }
  }
}

zx::status<> InspectDevfs::AddClassDirEntry(const fbl::RefPtr<Device>& dev) {
  // Create link in /dev/class/... if this id has a published class
  auto [dir, seqcount] = GetOrCreateProtoDir(dev->protocol_id());
  if (dir == nullptr) {
    // No class dir for this type, so ignore it
    return zx::ok();
  }
  std::optional file_opt = dev->inspect().file();
  if (!file_opt.has_value()) {
    // No inspect file for this device.
    return zx::ok();
  }
  fbl::RefPtr file = file_opt.value();

  char tmp[32];
  const char* name = nullptr;

  if (dev->protocol_id() != ZX_PROTOCOL_CONSOLE) {
    for (unsigned n = 0; n < 1000; n++) {
      snprintf(tmp, sizeof(tmp), "%03u.inspect", ((*seqcount)++) % 1000);
      fbl::RefPtr<fs::Vnode> node;
      if (dir->Lookup(tmp, &node) == ZX_ERR_NOT_FOUND) {
        name = tmp;
        break;
      }
    }
    if (name == nullptr) {
      return zx::error(ZX_ERR_ALREADY_EXISTS);
    }
  } else {
    snprintf(tmp, sizeof(tmp), "%.*s.inspect", static_cast<int>(dev->name().length()),
             dev->name().data());
    name = tmp;
  }

  zx::status<> status = zx::make_status(dir->AddEntry(name, file));
  if (status.is_error()) {
    return status;
  }
  dev->set_link_name(name);
  return zx::ok();
}

InspectManager::InspectManager(async_dispatcher_t* dispatcher) {
  auto driver_manager_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  driver_manager_dir->AddEntry("driver_host", driver_host_dir_);

  auto tree_handler = inspect::MakeTreeHandler(&inspector_, dispatcher);
  auto tree_service = fbl::MakeRefCounted<fs::Service>(
      [tree_handler = std::move(tree_handler)](zx::channel request) {
        tree_handler(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(request)));
        return ZX_OK;
      });
  driver_manager_dir->AddEntry(fuchsia::inspect::Tree::Name_, std::move(tree_service));

  diagnostics_dir_->AddEntry("driver_manager", driver_manager_dir);
  auto status = InspectDevfs::Create(diagnostics_dir_);
  ZX_ASSERT(status.is_ok());
  devfs_ = std::move(status.value());

  if (dispatcher) {
    diagnostics_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
  }

  devices_ = root_node().CreateChild("devices");
  device_count_ = root_node().CreateUint("device_count", 0);
}

zx::status<fidl::ClientEnd<fuchsia_io::Directory>> InspectManager::Connect() {
  zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto& [client, server] = endpoints.value();
  return zx::make_status(diagnostics_vfs_->ServeDirectory(diagnostics_dir_, std::move(server)),
                         std::move(client));
}

DeviceInspect::DeviceInspect(inspect::Node& devices, inspect::UintProperty& device_count,
                             std::string name, zx::vmo vmo)
    : device_count_node_(device_count) {
  // Devices are sometimes passed bogus handles. Fun!
  if (vmo.is_valid()) {
    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
    vmo_file_.emplace(fbl::MakeRefCounted<fs::VmoFile>(std::move(vmo), size));
  }
  device_node_ = devices.CreateChild(name);
  // Increment device count.
  device_count_node_.Add(1);

  // create properties with default values
  state_ = device_node_.CreateString("state", "");
  local_id_ = device_node_.CreateUint("driver_host_local_id", 0);
}

DeviceInspect::~DeviceInspect() {
  // Decrement device count.
  device_count_node_.Subtract(1);
}

void DeviceInspect::set_properties(const fbl::Array<const zx_device_prop_t>& props) {
  inspect::Node properties_array;

  // Add a node only if there are any `props`
  if (!props.empty()) {
    properties_array = device_node_.CreateChild("properties");
  }

  for (uint32_t i = 0; i < props.size(); ++i) {
    const zx_device_prop_t* p = &props[i];
    const char* param_name = di_bind_param_name(p->id);
    auto property = properties_array.CreateChild(std::to_string(i));
    property.CreateUint("value", p->value, &static_values_);
    if (param_name) {
      property.CreateString("id", param_name, &static_values_);
    } else {
      property.CreateString("id", std::to_string(p->id), &static_values_);
    }
    static_values_.emplace(std::move(property));
  }

  // Place the node into value list as props will not change in the lifetime of the device.
  if (!props.empty()) {
    static_values_.emplace(std::move(properties_array));
  }
}
