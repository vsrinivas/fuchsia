// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_

#include <fidl/fuchsia.component.runner/cpp/wire.h>
#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

// Note, all of the logic here assumes we are operating on a single-threaded
// dispatcher. It is not safe to use a multi-threaded dispatcher with this code.

class Node;

class DriverComponent : public fidl::WireServer<fuchsia_component_runner::ComponentController>,
                        public fidl::WireAsyncEventHandler<fuchsia_driver_framework::Driver>,
                        public fbl::DoublyLinkedListable<std::unique_ptr<DriverComponent>> {
 public:
  explicit DriverComponent(fidl::ClientEnd<fuchsia_driver_framework::Driver> driver,
                           async_dispatcher_t* dispatcher, std::string_view url);
  ~DriverComponent();

  std::string_view url() const;

  void set_driver_ref(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref);

  void set_node(std::shared_ptr<Node> node) { node_ = std::move(node); }

  // Request that this Driver be stopped. This will go through and
  // stop all of the Driver's children first.
  void RequestDriverStop();

  // Signal to the DriverHost that this Driver should be stopped.
  // This function should only be called after all of this Driver's children
  // have been stopped.
  // This should only be used by the Node class.
  void StopDriver();

 private:
  // This is called when fuchsia_driver_framework::Driver is closed.
  void on_fidl_error(fidl::UnbindInfo error) override;

  // fidl::WireServer<fuchsia_component_runner::ComponentController>
  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;
  void Kill(KillRequestView request, KillCompleter::Sync& completer) override;

  // Close the component connection to signal to CF that the component has stopped.
  // Once the component connection is closed, this class will eventually be
  // freed.
  void StopComponent();

  bool stop_in_progress_ = false;

  // The node that the Driver is bound to.
  // We want to keep the Node alive as long as the Driver is alive, because
  // the Driver must make sure that all of it's children are stopped before
  // it is stopped.
  // Also, a Node will not finish it's Remove process while it has a driver
  // bound to it.
  std::shared_ptr<Node> node_;

  // This channel represents the Driver in the DriverHost. If we call
  // Stop() on this channel, the DriverHost will call Stop on the Driver
  // and drop its end of the channel when it is finished.
  // When the other end of this channel is dropped, DriverComponent will
  // signal to ComponentFramework that the component has stopped.
  fidl::WireSharedClient<fuchsia_driver_framework::Driver> driver_;

  // This represents the Driver Component within the Component Framework.
  // When this is closed with an epitath it signals to the Component Framework
  // that this driver component has stopped.
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>> driver_ref_;

  // URL of the driver's component manifest
  std::string url_;
};

class DriverHostComponent : public fbl::DoublyLinkedListable<std::unique_ptr<DriverHostComponent>> {
 public:
  DriverHostComponent(fidl::ClientEnd<fuchsia_driver_framework::DriverHost> driver_host,
                      async_dispatcher_t* dispatcher,
                      fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts);

  zx::status<fidl::ClientEnd<fuchsia_driver_framework::Driver>> Start(
      fidl::ClientEnd<fuchsia_driver_framework::Node> client_end, const Node& node,
      fuchsia_component_runner::wire::ComponentStartInfo start_info);

 private:
  fidl::WireSharedClient<fuchsia_driver_framework::DriverHost> driver_host_;
};

enum class Collection {
  kNone,
  // Collection for driver hosts.
  kHost,
  // Collection for boot drivers.
  kBoot,
  // Collection for package drivers.
  kPackage,
};

// TODO(fxbug.dev/66150): Once FIDL wire types support a Clone() method,
// stop encoding and decoding messages as a workaround.
template <typename T>
class OwnedMessage {
 public:
  static std::unique_ptr<OwnedMessage<T>> From(T& message) {
    // TODO(fxbug.dev/45252): Use FIDL at rest.
    fidl::unstable::OwnedEncodedMessage<T> encoded(fidl::internal::WireFormatVersion::kV1,
                                                   &message);
    ZX_ASSERT_MSG(encoded.ok(), "Failed to encode: %s", encoded.FormatDescription().data());
    return std::make_unique<OwnedMessage>(encoded);
  }

  T& get() { return *decoded_.PrimaryObject(); }

 private:
  friend std::unique_ptr<OwnedMessage<T>> std::make_unique<OwnedMessage<T>>(
      fidl::unstable::OwnedEncodedMessage<T>&);

  // TODO(fxbug.dev/45252): Use FIDL at rest.
  explicit OwnedMessage(fidl::unstable::OwnedEncodedMessage<T>& encoded)
      : converted_(encoded.GetOutgoingMessage()),
        decoded_(fidl::internal::WireFormatVersion::kV1, std::move(converted_.incoming_message())) {
    ZX_ASSERT_MSG(decoded_.ok(), "Failed to decode: %s", decoded_.FormatDescription().c_str());
  }

  fidl::OutgoingToIncomingMessage converted_;
  fidl::unstable::DecodedMessage<T> decoded_;
};

class DriverBinder {
 public:
  virtual ~DriverBinder() = default;

  // Attempt to bind `node` with given `args`.
  virtual void Bind(Node& node, fuchsia_driver_framework::wire::NodeAddArgs args) = 0;
};

class Node : public fidl::WireServer<fuchsia_driver_framework::NodeController>,
             public fidl::WireServer<fuchsia_driver_framework::Node>,
             public std::enable_shared_from_this<Node> {
 public:
  using OwnedOffer = std::unique_ptr<OwnedMessage<fuchsia_component_decl::wire::Offer>>;

  Node(std::string_view name, std::vector<Node*> parents, DriverBinder* driver_binder,
       async_dispatcher_t* dispatcher);
  ~Node() override;

  const std::string& name() const;
  const std::optional<DriverComponent*>& driver_component() const;
  const std::vector<Node*>& parents() const;
  const std::vector<std::shared_ptr<Node>>& children() const;
  std::vector<OwnedOffer>& offers() const;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols() const;
  DriverHostComponent* driver_host() const;

  void set_collection(Collection collection);
  void set_driver_host(DriverHostComponent* driver_host);
  void set_node_ref(fidl::ServerBindingRef<fuchsia_driver_framework::Node> node_ref);
  void set_bound_driver_url(std::optional<std::string_view> bound_driver_url);
  void set_controller_ref(
      fidl::ServerBindingRef<fuchsia_driver_framework::NodeController> controller_ref);
  void set_driver_component(std::optional<DriverComponent*> driver_component);

  std::string TopoName() const;
  fidl::VectorView<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::AnyArena& arena) const;
  void OnBind() const;
  void AddToParents();

  // Begin the removal process for a Node. This function ensures that a Node is
  // only removed after all of its children are removed. It also ensures that
  // a Node is only removed after the driver that is bound to it has been stopped.
  // This is safe to call multiple times.
  // There are lots of reasons a Node's removal will be started:
  //   - The Node's driver component wants to exit.
  //   - The `node_ref` server has become unbound.
  //   - The Node's parent is being removed.
  void Remove();

 private:
  // fidl::WireServer<fuchsia_driver_framework::NodeController>
  void Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) override;
  // fidl::WireServer<fuchsia_driver_framework::Node>
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override;

  const std::string name_;
  std::vector<Node*> parents_;
  std::vector<std::shared_ptr<Node>> children_;
  fit::nullable<DriverBinder*> driver_binder_;
  async_dispatcher_t* const dispatcher_;

  fidl::Arena<128> arena_;
  std::vector<OwnedOffer> offers_;
  std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols_;

  Collection collection_ = Collection::kNone;
  fit::nullable<DriverHostComponent*> driver_host_;

  bool removal_in_progress_ = false;

  // If this exists, then this `driver_component_` is bound to this node.
  // The driver_component is not guaranteed to outlive the node, but if
  // the driver_component is freed, it will reset this field.
  std::optional<DriverComponent*> driver_component_;
  std::optional<std::string> bound_driver_url_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::NodeController>> controller_ref_;
};

class DriverRunner : public fidl::WireServer<fuchsia_component_runner::ComponentRunner>,
                     public DriverBinder {
 public:
  DriverRunner(fidl::ClientEnd<fuchsia_component::Realm> realm,
               fidl::ClientEnd<fuchsia_driver_framework::DriverIndex> driver_index,
               inspect::Inspector& inspector, async_dispatcher_t* dispatcher);

  fpromise::promise<inspect::Inspector> Inspect() const;
  size_t NumOrphanedNodes() const;
  zx::status<> PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> StartRootDriver(std::string_view url);
  const Node* root_node() const;

 private:
  using CompositeArgs = std::vector<std::weak_ptr<Node>>;
  using DriverUrl = std::string;
  using CompositeArgsIterator = std::unordered_multimap<DriverUrl, CompositeArgs>::iterator;

  // fidl::WireServer<fuchsia_component_runner::ComponentRunner>
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;
  // DriverBinder
  void Bind(Node& node, fuchsia_driver_framework::wire::NodeAddArgs args) override;

  // Create a composite node. Returns a `Node` that is owned by its parents.
  zx::status<Node*> CreateCompositeNode(
      Node& node, const fuchsia_driver_framework::wire::MatchedDriver& matched_driver);
  // Adds `matched_driver` to an existing set of composite arguments, or creates
  // a new set of composite arguments. Returns an iterator to the set of
  // composite arguments.
  zx::status<CompositeArgsIterator> AddToCompositeArgs(
      const std::string& name, const fuchsia_driver_framework::wire::MatchedDriver& matched_driver);
  zx::status<> StartDriver(Node& node, std::string_view url);

  zx::status<std::unique_ptr<DriverHostComponent>> StartDriverHost();

  struct CreateComponentOpts {
    const Node* node = nullptr;
    zx::handle token;
    fidl::ServerEnd<fuchsia_io::Directory> exposed_dir;
  };
  zx::status<> CreateComponent(std::string name, Collection collection, std::string url,
                               CreateComponentOpts opts);

  uint64_t next_driver_host_id_ = 0;
  fidl::WireClient<fuchsia_component::Realm> realm_;
  fidl::WireClient<fuchsia_driver_framework::DriverIndex> driver_index_;
  async_dispatcher_t* const dispatcher_;
  std::shared_ptr<Node> root_node_;

  std::unordered_map<zx_koid_t, Node&> driver_args_;
  std::unordered_multimap<DriverUrl, CompositeArgs> composite_args_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverComponent>> drivers_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>> driver_hosts_;

  // Orphaned nodes are nodes that have failed to bind to a driver, either
  // because no matching driver could be found, or because the matching driver
  // failed to start.
  std::vector<std::weak_ptr<Node>> orphaned_nodes_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
