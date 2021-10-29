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
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

// Note, all of the logic here assumes we are operating on a single-threaded
// dispatcher. It is not safe to use a multi-threaded dispatcher with this code.

class AsyncRemove;
class Node;

class DriverComponent : public fidl::WireServer<fuchsia_component_runner::ComponentController>,
                        public fbl::DoublyLinkedListable<std::unique_ptr<DriverComponent>> {
 public:
  explicit DriverComponent(fidl::ClientEnd<fuchsia_driver_framework::Driver> driver);

  void set_driver_ref(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref);

  zx::status<> Watch(async_dispatcher_t* dispatcher);

 private:
  // fidl::WireServer<fuchsia_component_runner::ComponentController>
  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;
  void Kill(KillRequestView request, KillCompleter::Sync& completer) override;

  void OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);

  fidl::ClientEnd<fuchsia_driver_framework::Driver> driver_;
  async::WaitMethod<DriverComponent, &DriverComponent::OnPeerClosed> wait_;
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>> driver_ref_;
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
  Node(std::string_view name, std::vector<Node*> parents, DriverBinder* driver_binder,
       async_dispatcher_t* dispatcher);
  ~Node() override;

  const std::string& name() const;
  const std::vector<std::shared_ptr<Node>>& children() const;
  std::vector<std::unique_ptr<fuchsia_component_decl::wire::Offer::DecodedMessage>>& offers() const;
  fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols() const;
  DriverHostComponent* driver_host() const;

  void set_collection(Collection collection);
  void set_driver_host(DriverHostComponent* driver_host);
  void set_driver_ref(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref);
  void set_node_ref(fidl::ServerBindingRef<fuchsia_driver_framework::Node> node_ref);
  void set_controller_ref(
      fidl::ServerBindingRef<fuchsia_driver_framework::NodeController> controller_ref);

  std::string TopoName() const;
  fidl::VectorView<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::AnyArena& arena) const;
  void OnBind() const;
  bool Unbind(std::unique_ptr<AsyncRemove>& async_remove);
  void Remove();
  void AddToParents();

 private:
  // fidl::WireServer<fuchsia_driver_framework::NodeController>
  void Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) override;
  // fidl::WireServer<fuchsia_driver_framework::Node>
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override;

  const std::string name_;
  const std::vector<Node*> parents_;
  std::vector<std::shared_ptr<Node>> children_;
  fit::nullable<DriverBinder*> driver_binder_;
  async_dispatcher_t* const dispatcher_;

  fidl::Arena<128> arena_;
  std::vector<std::unique_ptr<fuchsia_component_decl::wire::Offer::DecodedMessage>> offers_;
  std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols_;

  Collection collection_ = Collection::kNone;
  fit::nullable<DriverHostComponent*> driver_host_;
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>> driver_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::NodeController>> controller_ref_;
  std::unique_ptr<AsyncRemove> async_remove_;
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
  zx::status<> StartRootDriver(std::string_view name);

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
