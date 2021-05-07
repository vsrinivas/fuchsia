// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_

#include <fuchsia/component/runner/llcpp/fidl.h>
#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <fuchsia/sys2/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

// Note, all of the logic here assumes we are operating on a single-threaded
// dispatcher. It is not safe to use a multi-threaded dispatcher with this code.

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
      fidl::ClientEnd<fuchsia_driver_framework::Node> node,
      fidl::VectorView<fidl::StringView> offers,
      fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols, fidl::StringView url,
      fuchsia_data::wire::Dictionary program,
      fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry> ns,
      fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
      fidl::ClientEnd<fuchsia_io::Directory> exposed_dir);

 private:
  fidl::Client<fuchsia_driver_framework::DriverHost> driver_host_;
};

class AsyncRemove;
class Node;

class DriverBinder {
 public:
  virtual ~DriverBinder() = default;
  // Attempt to bind `node` with given `args`.
  // The lifetime of `node` must live until `callback` is called.
  virtual void Bind(Node& node, fuchsia_driver_framework::wire::NodeAddArgs args,
                    fit::callback<void(zx::status<>)> callback) = 0;
};

class Node : public fidl::WireServer<fuchsia_driver_framework::NodeController>,
             public fidl::WireServer<fuchsia_driver_framework::Node>,
             public std::enable_shared_from_this<Node> {
 public:
  Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher,
       std::string_view name);
  ~Node() override;

  const std::string& name() const;
  fidl::VectorView<fidl::StringView> offers();
  fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols();
  DriverHostComponent* parent_driver_host() const;
  void set_driver_host(DriverHostComponent* driver_host);
  void set_driver_ref(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref);
  void set_node_ref(fidl::ServerBindingRef<fuchsia_driver_framework::Node> node_ref);
  void set_controller_ref(
      fidl::ServerBindingRef<fuchsia_driver_framework::NodeController> controller_ref);
  const std::vector<std::shared_ptr<Node>>& children() const;

  std::string TopoName() const;
  bool Unbind(std::unique_ptr<AsyncRemove>& async_remove);
  void Remove();

 private:
  // fidl::WireServer<fuchsia_driver_framework::NodeController>
  void Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) override;
  // fidl::WireServer<fuchsia_driver_framework::Node>
  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override;

  Node* const parent_;
  // This can be null when Remove() is called.
  DriverBinder* driver_binder_;
  async_dispatcher_t* const dispatcher_;

  const std::string name_;
  fidl::FidlAllocator<512> allocator_;
  std::vector<fidl::StringView> offers_;
  std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols_;

  DriverHostComponent* driver_host_ = nullptr;
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>> driver_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_ref_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::NodeController>> controller_ref_;
  std::unique_ptr<AsyncRemove> async_remove_;
  std::vector<std::shared_ptr<Node>> children_;
};

struct DriverArgs {
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir;
  Node& node;
};

class DriverRunner : public fidl::WireServer<fuchsia_component_runner::ComponentRunner>,
                     public DriverBinder {
 public:
  DriverRunner(fidl::ClientEnd<fuchsia_sys2::Realm> realm,
               fidl::ClientEnd<fuchsia_driver_framework::DriverIndex> driver_index,
               inspect::Inspector& inspector, async_dispatcher_t* dispatcher);

  fit::promise<inspect::Inspector> Inspect();
  zx::status<> PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> StartRootDriver(std::string_view name);

 private:
  // fidl::WireServer<fuchsia_component_runner::ComponentRunner>
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;
  // DriverBinder
  void Bind(Node& node, fuchsia_driver_framework::wire::NodeAddArgs args,
            fit::callback<void(zx::status<>)> callback) override;

  zx::status<> StartDriver(Node& node, std::string_view url);

  zx::status<std::unique_ptr<DriverHostComponent>> StartDriverHost();
  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> CreateComponent(std::string name,
                                                                     std::string url,
                                                                     std::string collection);
  uint64_t next_driver_host_id_ = 0;
  fidl::Client<fuchsia_sys2::Realm> realm_;
  fidl::Client<fuchsia_driver_framework::DriverIndex> driver_index_;
  async_dispatcher_t* dispatcher_;
  Node root_node_;
  std::unordered_map<std::string, DriverArgs> driver_args_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverComponent>> drivers_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>> driver_hosts_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
