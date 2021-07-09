// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_

#include <lib/ddk/binding.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <fbl/array.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

class Device;

struct ProtocolInfo {
  const char* name;
  fbl::RefPtr<fs::PseudoDir> devnode;
  uint32_t id;
  uint32_t flags;
  uint32_t seqcount;
};

static const inline ProtocolInfo kProtoInfos[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) {name, nullptr, val, flags, 0},
#include <lib/ddk/protodefs.h>
};

class InspectDevfs {
 public:
  // Use Create instead.
  explicit InspectDevfs(const fbl::RefPtr<fs::PseudoDir>& root_dir,
                        fbl::RefPtr<fs::PseudoDir> class_dir);

  static zx::status<InspectDevfs> Create(const fbl::RefPtr<fs::PseudoDir>& root_dir);

  std::tuple<fbl::RefPtr<fs::PseudoDir>, uint32_t*> GetProtoDir(uint32_t id);
  // Get protocol |id| directory if it exists, else create one.
  std::tuple<fbl::RefPtr<fs::PseudoDir>, uint32_t*> GetOrCreateProtoDir(uint32_t id);
  // Delete protocol |id| directory if no files are present.
  void RemoveEmptyProtoDir(uint32_t id);

  zx::status<> AddClassDirEntry(const fbl::RefPtr<Device>& dev);

  // Initialize |dev|'s devfs state
  zx::status<> InitInspectFile(const fbl::RefPtr<Device>& dev);

  zx::status<> Publish(const fbl::RefPtr<Device>& dev);

  // Convenience method for initializing |dev| and publishing it to devfs immediately.
  zx::status<> InitInspectFileAndPublish(const fbl::RefPtr<Device>& dev);

  void Unpublish(Device* dev);

 private:
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fbl::RefPtr<fs::PseudoDir> class_dir_;
  std::array<ProtocolInfo, std::size(kProtoInfos)> proto_infos_;
};

class InspectManager {
 public:
  explicit InspectManager(async_dispatcher_t* dispatcher);

  InspectManager() = delete;

  fidl::UnownedClientEnd<fuchsia_io::Directory> diagnostics_client() {
    return diagnostics_client_.borrow();
  }

  fs::PseudoDir& diagnostics_dir() { return *diagnostics_dir_; }

  fbl::RefPtr<fs::PseudoDir> driver_host_dir() { return driver_host_dir_; }

  inspect::Node& root_node() { return inspector_.GetRoot(); }

  inspect::Node& devices() { return devices_; }

  inspect::UintProperty& device_count() { return device_count_; }

  inspect::Inspector& inspector() { return inspector_; }

  std::optional<InspectDevfs>& devfs() { return devfs_; }

 private:
  inspect::Inspector inspector_;

  std::unique_ptr<fs::SynchronousVfs> diagnostics_vfs_;
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  fbl::RefPtr<fs::PseudoDir> driver_host_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();

  fidl::ClientEnd<fuchsia_io::Directory> diagnostics_client_;

  inspect::UintProperty device_count_;
  inspect::Node devices_;

  // The inspect devfs instance
  std::optional<InspectDevfs> devfs_;
};

class DeviceInspect {
 public:
  // |devices| and |device_count| should outlive DeviceInspect class
  DeviceInspect(inspect::Node& devices, inspect::UintProperty& device_count, std::string name,
                zx::vmo inspect_vmo);

  ~DeviceInspect();

  inspect::Node& device_node() { return device_node_; }

  zx::vmo& vmo() { return vmo_; }

  void set_state(std::string state) { state_.Set(state); }

  void set_local_id(uint64_t local_id) { local_id_.Set(local_id); }

  // These methods below are for static values and should be called only once. Calling it more than
  // once will lead to duplicate entries.

  void set_topological_path(std::string path) {
    device_node_.CreateString("topological_path", path, &static_values_);
  }

  void set_protocol_id(uint32_t value) {
    device_node_.CreateUint("protocol_id", value, &static_values_);
  }

  void set_type(std::string type) { device_node_.CreateString("type", type, &static_values_); }

  void set_flags(uint32_t flags) { device_node_.CreateUint("flags", flags, &static_values_); }

  void set_properties(const fbl::Array<const zx_device_prop_t>& props);

  void set_driver(std::string libname) {
    device_node_.CreateString("driver", libname, &static_values_);
  }

 private:
  inspect::UintProperty& device_count_node_;
  inspect::Node device_node_;

  // Reference to nodes with static properties
  inspect::ValueList static_values_;

  inspect::StringProperty state_;
  // Unique id of the device in a driver host
  inspect::UintProperty local_id_;

  // Inspect VMO returned via devfs's inspect nodes.
  zx::vmo vmo_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
