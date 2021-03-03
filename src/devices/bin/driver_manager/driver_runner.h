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
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

// Note, all of the logic here assumes we are operating on a single-threaded
// dispatcher. It is not safe to use a multi-threaded dispatcher with this code.

class DriverComponent : public fuchsia_component_runner::ComponentController::Interface,
                        public fbl::DoublyLinkedListable<std::unique_ptr<DriverComponent>> {
 public:
  DriverComponent(fidl::ClientEnd<fuchsia_io::Directory> exposed_dir,
                  fidl::ClientEnd<fuchsia_driver_framework::Driver> driver);
  ~DriverComponent() override;

  void set_driver_binding(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_binding);
  void set_node_binding(fidl::ServerBindingRef<fuchsia_driver_framework::Node> node_binding);

  zx::status<> WatchDriver(async_dispatcher_t* dispatcher);

 private:
  // fuchsia_component_runner::ComponentController::Interface
  void Stop(StopCompleter::Sync& completer) override;
  void Kill(KillCompleter::Sync& completer) override;

  void OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);

  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir_;
  fidl::ClientEnd<fuchsia_driver_framework::Driver> driver_;
  async::WaitMethod<DriverComponent, &DriverComponent::OnPeerClosed> wait_;
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>>
      driver_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_binding_;
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

class Node;

class DriverBinder {
 public:
  virtual ~DriverBinder() = default;
  virtual zx::status<> Bind(Node* node, fuchsia_driver_framework::wire::NodeAddArgs args) = 0;
};

class Node : public fuchsia_driver_framework::NodeController::Interface,
             public fuchsia_driver_framework::Node::Interface,
             public fbl::DoublyLinkedListable<std::unique_ptr<Node>> {
 public:
  using Offers = std::vector<fidl::StringView>;
  using Symbols = std::vector<fuchsia_driver_framework::wire::NodeSymbol>;

  Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher,
       std::string_view name);
  ~Node() override;

  const std::string& name() const;
  fidl::VectorView<fidl::StringView> offers();
  fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols();
  DriverHostComponent* parent_driver_host() const;
  void set_driver_host(DriverHostComponent* driver_host);
  void set_driver_binding(
      fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_binding);
  void set_node_binding(fidl::ServerBindingRef<fuchsia_driver_framework::Node> node_binding);
  void set_controller_binding(
      fidl::ServerBindingRef<fuchsia_driver_framework::NodeController> controller_binding);
  fbl::DoublyLinkedList<std::unique_ptr<Node>>& children();

  void Remove();

 private:
  void Unbind();
  // fuchsia_driver_framework::NodeController::Interface
  void Remove(RemoveCompleter::Sync& completer) override;
  // fuchsia_driver_framework::Node::Interface
  void AddChild(fuchsia_driver_framework::wire::NodeAddArgs args,
                fidl::ServerEnd<fuchsia_driver_framework::NodeController> controller,
                fidl::ServerEnd<fuchsia_driver_framework::Node> node,
                AddChildCompleter::Sync& completer) override;

  Node* const parent_;
  DriverBinder* const driver_binder_;
  async_dispatcher_t* const dispatcher_;

  const std::string name_;
  fidl::FidlAllocator<512> allocator_;
  Offers offers_;
  Symbols symbols_;

  DriverHostComponent* driver_host_ = nullptr;
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>>
      driver_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Node>> node_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::NodeController>>
      controller_binding_;
  fbl::DoublyLinkedList<std::unique_ptr<Node>> children_;
};

struct MatchResult {
  std::string url;
  fuchsia_driver_framework::wire::NodeAddArgs matched_args;
};

// TODO(fxbug.dev/33183): Replace this with a driver_index component.
class DriverIndex {
 public:
  using MatchCallback =
      fit::function<zx::status<MatchResult>(fuchsia_driver_framework::wire::NodeAddArgs args)>;
  explicit DriverIndex(MatchCallback match_callback);

  zx::status<MatchResult> Match(fuchsia_driver_framework::wire::NodeAddArgs args);

 private:
  MatchCallback match_callback_;
};

struct DriverArgs {
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir;
  Node* node;
};

class DriverRunner : public fuchsia_component_runner::ComponentRunner::Interface,
                     public DriverBinder {
 public:
  DriverRunner(fidl::ClientEnd<fuchsia_sys2::Realm> realm, DriverIndex* driver_index,
               inspect::Inspector* inspector, async_dispatcher_t* dispatcher);

  fit::promise<inspect::Inspector> Inspect();
  zx::status<> PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> StartRootDriver(std::string_view name);

 private:
  // fuchsia_component_runner::ComponentRunner::Interface
  void Start(fuchsia_component_runner::wire::ComponentStartInfo start_info,
             fidl::ServerEnd<fuchsia_component_runner::ComponentController> controller,
             StartCompleter::Sync& completer) override;
  // DriverBinder
  zx::status<> Bind(Node* node, fuchsia_driver_framework::wire::NodeAddArgs args) override;

  zx::status<std::unique_ptr<DriverHostComponent>> StartDriverHost();
  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> CreateComponent(std::string name,
                                                                     std::string url,
                                                                     std::string collection);
  uint64_t NextId();

  uint64_t next_id_ = 0;
  fidl::Client<fuchsia_sys2::Realm> realm_;
  DriverIndex* driver_index_;
  async_dispatcher_t* dispatcher_;
  Node root_node_;
  std::unordered_map<std::string, DriverArgs> driver_args_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverComponent>> drivers_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>> driver_hosts_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
