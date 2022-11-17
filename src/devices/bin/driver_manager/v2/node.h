// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_H_

#include <fidl/fuchsia.component.runner/cpp/wire.h>
#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.host/cpp/wire.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <list>

#include "src/devices/bin/driver_manager/v2/driver_host.h"

namespace dfv2 {

// This function creates a composite offer based on a service offer.
std::optional<fuchsia_component_decl::wire::Offer> CreateCompositeServiceOffer(
    fidl::AnyArena& arena, fuchsia_component_decl::wire::Offer& offer,
    std::string_view parents_name, bool primary_parent);

class Node;
class NodeRemovalTracker;

using NodeBindingInfoResultCallback =
    fit::callback<void(fidl::VectorView<fuchsia_driver_development::wire::NodeBindingInfo>)>;

class BindResultTracker {
 public:
  explicit BindResultTracker(size_t expected_result_count,
                             NodeBindingInfoResultCallback result_callback);

  void ReportSuccessfulBind(const std::string_view& node_name, const std::string_view& driver);
  void ReportNoBind();

 private:
  void Complete(size_t current);
  fidl::Arena<> arena_;
  size_t expected_result_count_;
  size_t currently_reported_ TA_GUARDED(lock_);
  std::mutex lock_;
  NodeBindingInfoResultCallback result_callback_;
  std::vector<fuchsia_driver_development::wire::NodeBindingInfo> results_;
};

class NodeManager {
 public:
  virtual ~NodeManager() = default;

  // Attempt to bind `node`.
  // A nullptr for result_tracker is acceptable if the caller doesn't intend to
  // track the results.
  virtual void Bind(Node& node, std::shared_ptr<BindResultTracker> result_tracker) = 0;

  virtual zx::result<DriverHost*> CreateDriverHost() = 0;
};

enum class Collection {
  kNone,
  // Collection for driver hosts.
  kHost,
  // Collection for boot drivers.
  kBoot,
  // Collection for package drivers.
  kPackage,
  // Collection for universe package drivers.
  kUniversePackage,
};

enum class RemovalSet {
  kAll,      // Remove the boot drivers and the package drivers
  kPackage,  // Remove the package drivers
};

enum class NodeState {
  kRunning,            // Normal running state.
  kPrestop,            // Still running, but will remove soon. usually because
                       //  Received Remove(kPackage), but is a boot driver.
  kWaitingOnChildren,  // Received Remove, and waiting for children to be removed.
  kWaitingOnDriver,    // Waiting for driver to respond from Stop() command.
  kStopping,           // finishing shutdown of node.
};
class Node : public fidl::WireServer<fuchsia_driver_framework::NodeController>,
             public fidl::WireServer<fuchsia_driver_framework::Node>,
             public fidl::WireServer<fuchsia_component_runner::ComponentController>,
             public fidl::WireAsyncEventHandler<fuchsia_driver_host::Driver>,
             public std::enable_shared_from_this<Node> {
 public:
  Node(std::string_view name, std::vector<Node*> parents, NodeManager* node_manager,
       async_dispatcher_t* dispatcher, uint32_t primary_index = 0);
  Node(std::string_view name, std::vector<Node*> parents, NodeManager* node_manager,
       async_dispatcher_t* dispatcher, DriverHost* driver_host);

  ~Node() override;

  static zx::result<std::shared_ptr<Node>> CreateCompositeNode(
      std::string_view node_name, std::vector<Node*> parents,
      std::vector<std::string> parents_names,
      std::vector<fuchsia_driver_framework::wire::NodeProperty> properties,
      NodeManager* driver_binder, async_dispatcher_t* dispatcher, uint32_t primary_index = 0);

  fuchsia_driver_framework::wire::NodeAddArgs CreateAddArgs(fidl::AnyArena& arena);

  void OnBind() const;

  // Begin the removal process for a Node. This function ensures that a Node is
  // only removed after all of its children are removed. It also ensures that
  // a Node is only removed after the driver that is bound to it has been stopped.
  // This is safe to call multiple times.
  // There are multiple reasons a Node's removal will be started:
  //   - The system is being stopped.
  //   - The Node had an unexpected error or disconnect
  // During a system stop, Remove is expected to be called twice:
  // once with |removal_set| == kPackage, and once with |removal_set| == kAll.
  // Errors and disconnects that are unrecoverable should call Remove(kAll, nullptr).
  void Remove(RemovalSet removal_set, NodeRemovalTracker* removal_tracker);

  fit::result<fuchsia_driver_framework::wire::NodeError, std::shared_ptr<Node>> AddChild(
      fuchsia_driver_framework::wire::NodeAddArgs args,
      fidl::ServerEnd<fuchsia_driver_framework::NodeController> controller,
      fidl::ServerEnd<fuchsia_driver_framework::Node> node);

  zx::result<> StartDriver(
      fuchsia_component_runner::wire::ComponentStartInfo start_info,
      fidl::ServerEnd<fuchsia_component_runner::ComponentController> controller);

  bool IsComposite() const;

  // Exposed for testing.
  Node* GetPrimaryParent() const;

  const std::string& name() const;
  const DriverHost* driver_host() const { return *driver_host_; }
  const std::string& driver_url() const;
  const std::vector<Node*>& parents() const;
  const std::list<std::shared_ptr<Node>>& children() const;
  fidl::ArenaBase& arena() { return arena_; }
  fidl::VectorView<fuchsia_component_decl::wire::Offer> offers() const;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols() const;
  const std::vector<fuchsia_driver_framework::wire::NodeProperty>& properties() const;

  void set_collection(Collection collection);
  void set_offers(std::vector<fuchsia_component_decl::wire::Offer> offers) {
    offers_ = std::move(offers);
  }
  void set_symbols(std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols) {
    symbols_ = std::move(symbols);
  }

  std::string TopoName() const;

 private:
  struct DriverComponent {
    // This represents the Driver Component within the Component Framework.
    // When this is closed with an epitaph it signals to the Component Framework
    // that this driver component has stopped.
    fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> component_controller_ref;
    std::optional<fidl::WireSharedClient<fuchsia_driver_host::Driver>> driver;
    std::string driver_url;
  };
  // This is called when fuchsia_driver_framework::Driver is closed.
  void on_fidl_error(fidl::UnbindInfo error) override;
  // fidl::WireServer<fuchsia_component_runner::ComponentController>
  // We ignore these signals.
  void Stop(StopCompleter::Sync& completer) override;
  void Kill(KillCompleter::Sync& completer) override;
  // fidl::WireServer<fuchsia_driver_framework::NodeController>
  void Remove(RemoveCompleter::Sync& completer) override;
  // fidl::WireServer<fuchsia_driver_framework::Node>
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override;

  // Add this Node to its parents. This should be called when the node is created.
  void AddToParents();

  // Shutdown helpers:
  // Remove a child from this parent
  void RemoveChild(std::shared_ptr<Node> child);
  // Check to see if all children have been removed.  If so, move on to stopping driver.
  void CheckForRemoval();
  // After stopping driver, cleanup and remove node.
  void FinishRemoval();

  // Close the component connection to signal to CF that the component has stopped.
  void StopComponent();

  // The node's original name. This should be used for exporting to devfs.
  // TODO(fxbug.dev/111156): Migrate driver names to only use CF valid characters and simplify
  //  this logic.
  std::string devfs_name_;
  // The node's name which is valid for CF.
  // This has been transformed from the original name, ":" and "/" have been replaced.
  std::string name_;

  // If this is a composite device, this stores the list of each parent's names.
  std::vector<std::string> parents_names_;
  std::vector<Node*> parents_;
  uint32_t primary_index_ = 0;
  std::list<std::shared_ptr<Node>> children_;
  fit::nullable<NodeManager*> node_manager_;
  async_dispatcher_t* const dispatcher_;

  fidl::Arena<128> arena_;
  std::vector<fuchsia_component_decl::wire::Offer> offers_;
  std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols_;
  std::vector<fuchsia_driver_framework::wire::NodeProperty> properties_;

  Collection collection_ = Collection::kNone;
  fit::nullable<DriverHost*> driver_host_;

  NodeState node_state_ = NodeState::kRunning;
  NodeRemovalTracker* removal_tracker_ = nullptr;

  std::optional<DriverComponent> driver_component_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::NodeController>> controller_ref_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_H_
