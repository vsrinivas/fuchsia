// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_COMPAT_H_
#define SRC_DEVICES_LIB_COMPAT_COMPAT_H_

#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/fpromise/promise.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include <unordered_map>
#include <unordered_set>

#include "src/devices/lib/compat/service.h"
#include "src/devices/lib/compat/symbols.h"
#include "src/devices/lib/driver2/devfs_exporter.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/start_args.h"

namespace compat {

using Metadata = std::vector<uint8_t>;
using MetadataMap = std::unordered_map<uint32_t, const Metadata>;

// The DeviceServer class vends the fuchsia_driver_compat::Device interface.
// It represents a single device.
class DeviceServer : public fidl::WireServer<fuchsia_driver_compat::Device> {
 public:
  DeviceServer(std::string topological_path, MetadataMap metadata)
      : topological_path_(std::move(topological_path)), metadata_(std::move(metadata)) {}

  // Functions to implement the DFv1 device API.
  zx_status_t AddMetadata(uint32_t type, const void* data, size_t size);
  zx_status_t GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual);
  zx_status_t GetMetadataSize(uint32_t type, size_t* out_size);

 private:
  // fuchsia.driver.compat.Compat
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) override;

  std::string topological_path_;
  MetadataMap metadata_;
};

class Child;

// The Interop class holds information about what this component is exposing in its namespace.
// This class is used to expose things in the outgoing namespace in a way that a child
// compat driver can understand.
class Interop {
 public:
  // Create an Interop. Each parameter here is an unowned pointer, so these objects must outlive
  // the Interop class.
  static zx::status<Interop> Create(async_dispatcher_t* dispatcher, const driver::Namespace* ns,
                                    service::OutgoingDirectory* outgoing);

  // Start a promise to connect to the parent's compat service.
  // When this returns successful, `device_client` will be correctly set.
  fpromise::promise<void, zx_status_t> ConnectToParentCompatService();

  // Take a Child, and export its fuchsia.driver.compat service, and it export it to devfs.
  fpromise::promise<void, zx_status_t> ExportChild(Child* child);

  fidl::WireSharedClient<fuchsia_driver_compat::Device>& device_client() { return device_client_; }

 private:
  friend class Child;

  async_dispatcher_t* dispatcher_;
  const driver::Namespace* ns_;
  service::OutgoingDirectory* outgoing_;

  fbl::RefPtr<fs::PseudoDir> compat_service_;
  fidl::WireSharedClient<fuchsia_driver_compat::Device> device_client_;
  driver::DevfsExporter exporter_;
};

// The Child class represents a child device.
// When a Child is removed, it will remove the services it added in the
// outgoing directory.
class Child {
 public:
  Child(std::string name, uint32_t proto_id, std::string topological_path,
        fbl::RefPtr<fs::Vnode> dev_vnode, MetadataMap metadata)
      : topological_path_(std::move(topological_path)),
        name_(std::move(name)),
        proto_id_(proto_id),
        compat_device_(topological_path_, std::move(metadata)),
        dev_vnode_(std::move(dev_vnode)) {}

  // Export this Child to /dev/. When this promise returns successfully, this
  // child is exported to /dev/.
  fpromise::promise<void, zx_status_t> ExportToDevfs(driver::DevfsExporter& exporter);

  DeviceServer& compat_device() { return compat_device_; }
  std::string_view name() { return name_; }
  fbl::RefPtr<fs::Vnode>& dev_vnode() { return dev_vnode_; }

  void AddInstance(std::unique_ptr<OwnedInstance> instance) {
    instances_.push_back(std::move(instance));
  }
  void AddProtocol(std::unique_ptr<OwnedProtocol> protocol) {
    protocols_.push_back(std::move(protocol));
  }

  // Create a vector of offers based on the service instances that have been added to the child.
  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);

 private:
  std::string topological_path_;
  std::string name_;
  uint32_t proto_id_;

  DeviceServer compat_device_;
  fbl::RefPtr<fs::Vnode> dev_vnode_;

  std::vector<std::unique_ptr<OwnedInstance>> instances_;
  std::vector<std::unique_ptr<OwnedProtocol>> protocols_;
};

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_COMPAT_H_
