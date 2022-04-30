// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_COMPAT_H_
#define SRC_DEVICES_LIB_COMPAT_COMPAT_H_

#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/start_args.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/promise.h>
#include <lib/service/llcpp/service_handler.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <unordered_map>
#include <unordered_set>

#include "src/devices/lib/compat/symbols.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace compat {

// This represents a protocol that a driver is offering to a child driver.
struct ProtocolOffer {
  // The name of the protocol being offered. The driver is responsible for
  // making sure this protocol has been exported in it's outgoing directory.
  std::string protocol_name;
  // A callback that will be called when this protocol is going out of scope.
  // This is useful if a driver is exposing a protocol to multiple children,
  // and would like to perform cleanup if all children are removed.
  std::shared_ptr<fit::deferred_callback> remove_protocol_callback;
};

// This represents a service instance that a driver is offering to a child driver.
struct ServiceInstanceOffer {
  // The name of the service being offered. The driver is responsible for
  // making sure this service has been exported in its outgoing directory.
  std::string service_name;
  // The name of the instance being offered. The driver is responsible
  // for making sure this instance exists in the service it has exported.
  std::string instance_name;
  // Optional: If this exists, then this is the instance name the child
  // driver will see.
  std::optional<std::string> renamed_instance_name;
  // A callback that will be called when this offer is going out of scope.
  // This is useful if a driver is exposing an instance to multiple children,
  // and would like to perform cleanup if all children are removed.
  std::shared_ptr<fit::deferred_callback> remove_service_callback;
};

class ChildOffers {
 public:
  void AddProtocol(ProtocolOffer offer) { protocol_offers_.push_back(std::move(offer)); }

  void AddServiceInstance(ServiceInstanceOffer offer) {
    instance_offers_.push_back(std::move(offer));
  }

  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);

 private:
  std::vector<ProtocolOffer> protocol_offers_;
  std::vector<ServiceInstanceOffer> instance_offers_;
};

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

  void set_dir(fidl::ClientEnd<fuchsia_io::Directory> dir) { dir_ = std::move(dir); }

 private:
  // fuchsia.driver.compat.Compat
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) override;
  void ConnectFidl(ConnectFidlRequestView request, ConnectFidlCompleter::Sync& completer) override;

  std::string topological_path_;
  MetadataMap metadata_;
  fidl::ClientEnd<fuchsia_io::Directory> dir_;
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
                                    component::OutgoingDirectory* outgoing);

  // Take a Child, and export its fuchsia.driver.compat service, and it export it to devfs.
  fpromise::promise<void, zx_status_t> ExportChild(Child* child, fbl::RefPtr<fs::Vnode> dev_node);

 private:
  friend class Child;

  async_dispatcher_t* dispatcher_;
  const driver::Namespace* ns_;
  component::OutgoingDirectory* outgoing_;

  std::unique_ptr<fs::SynchronousVfs> vfs_;
  fbl::RefPtr<fs::PseudoDir> devfs_exports_;
  fbl::RefPtr<fs::PseudoDir> compat_service_;
  driver::DevfsExporter exporter_;
};

zx::status<fidl::WireSharedClient<fuchsia_driver_compat::Device>> ConnectToParentDevice(
    async_dispatcher_t* dispatcher, const driver::Namespace* ns, std::string_view name = "default");

// The Child class represents a child device.
// When a Child is removed, it will remove the services it added in the
// outgoing directory.
class Child {
 public:
  Child(std::string name, uint32_t proto_id, std::string topological_path, MetadataMap metadata)
      : topological_path_(std::move(topological_path)),
        name_(std::move(name)),
        proto_id_(proto_id),
        compat_device_(topological_path_, std::move(metadata)) {}

  DeviceServer& compat_device() { return compat_device_; }
  std::string_view name() { return name_; }
  std::string_view topological_path() { return topological_path_; }
  uint32_t proto_id() { return proto_id_; }
  ChildOffers& offers() { return offers_; }

  // This is a way to give the child shared ownership over something.
  // When child is removed, there will be one less reference to callback,
  // and callback will be called when all references are removed.
  void AddCallback(std::shared_ptr<fit::deferred_callback> callback) {
    callbacks_.push_back(std::move(callback));
  }

  // Create a vector of offers based on the service instances that have been added to the child.
  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);

 private:
  std::string topological_path_;
  std::string name_;
  uint32_t proto_id_;

  DeviceServer compat_device_;
  ChildOffers offers_;

  // A list of callbacks to potentially call when this class is destructed.
  std::vector<std::shared_ptr<fit::deferred_callback>> callbacks_;
};

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_COMPAT_H_
