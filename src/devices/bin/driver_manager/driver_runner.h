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

class DriverComponent : public llcpp::fuchsia::component::runner::ComponentController::Interface,
                        public fbl::DoublyLinkedListable<std::unique_ptr<DriverComponent>> {
 public:
  DriverComponent(fidl::ClientEnd<llcpp::fuchsia::io::Directory> exposed_dir,
                  fidl::ClientEnd<llcpp::fuchsia::driver::framework::Driver> driver);
  ~DriverComponent() override;

  void set_driver_binding(
      fidl::ServerBindingRef<llcpp::fuchsia::component::runner::ComponentController>
          driver_binding);
  void set_node_binding(
      fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node> node_binding);

  zx::status<> WatchDriver(async_dispatcher_t* dispatcher);

 private:
  // llcpp::fuchsia::component::runner::ComponentController::Interface
  void Stop(StopCompleter::Sync& completer) override;
  void Kill(KillCompleter::Sync& completer) override;

  void OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);

  fidl::ClientEnd<llcpp::fuchsia::io::Directory> exposed_dir_;
  fidl::ClientEnd<llcpp::fuchsia::driver::framework::Driver> driver_;
  async::WaitMethod<DriverComponent, &DriverComponent::OnPeerClosed> wait_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::component::runner::ComponentController>>
      driver_binding_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node>> node_binding_;
};

class DriverHostComponent : public fbl::DoublyLinkedListable<std::unique_ptr<DriverHostComponent>> {
 public:
  DriverHostComponent(fidl::ClientEnd<llcpp::fuchsia::driver::framework::DriverHost> driver_host,
                      async_dispatcher_t* dispatcher,
                      fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts);

  zx::status<fidl::ClientEnd<llcpp::fuchsia::driver::framework::Driver>> Start(
      fidl::ClientEnd<llcpp::fuchsia::driver::framework::Node> node,
      fidl::VectorView<fidl::StringView> offers,
      fidl::VectorView<llcpp::fuchsia::driver::framework::NodeSymbol> symbols, fidl::StringView url,
      llcpp::fuchsia::data::Dictionary program,
      fidl::VectorView<llcpp::fuchsia::component::runner::ComponentNamespaceEntry> ns,
      fidl::ServerEnd<llcpp::fuchsia::io::Directory> outgoing_dir,
      fidl::ClientEnd<llcpp::fuchsia::io::Directory> exposed_dir);

 private:
  fidl::Client<llcpp::fuchsia::driver::framework::DriverHost> driver_host_;
};

class Node;

class DriverBinder {
 public:
  virtual ~DriverBinder() = default;
  virtual zx::status<> Bind(Node* node, llcpp::fuchsia::driver::framework::NodeAddArgs args) = 0;
};

class Node : public llcpp::fuchsia::driver::framework::NodeController::Interface,
             public llcpp::fuchsia::driver::framework::Node::Interface,
             public fbl::DoublyLinkedListable<std::unique_ptr<Node>> {
 public:
  using Offers = std::vector<fidl::StringView>;
  using Symbols = std::vector<llcpp::fuchsia::driver::framework::NodeSymbol>;

  Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher,
       std::string_view name, Offers offers, Symbols symbols);
  ~Node() override;

  const std::string& name() const;
  fidl::VectorView<fidl::StringView> offers();
  fidl::VectorView<llcpp::fuchsia::driver::framework::NodeSymbol> symbols();
  DriverHostComponent* parent_driver_host() const;
  void set_driver_host(DriverHostComponent* driver_host);
  void set_driver_binding(
      fidl::ServerBindingRef<llcpp::fuchsia::component::runner::ComponentController>
          driver_binding);
  void set_node_binding(
      fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node> node_binding);
  void set_controller_binding(
      fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::NodeController> controller_binding);
  fbl::DoublyLinkedList<std::unique_ptr<Node>>& children();

  void Remove();

 private:
  void Unbind();
  // llcpp::fuchsia::driver::framework::NodeController::Interface
  void Remove(RemoveCompleter::Sync& completer) override;
  // llcpp::fuchsia::driver::framework::Node::Interface
  void AddChild(llcpp::fuchsia::driver::framework::NodeAddArgs args,
                fidl::ServerEnd<llcpp::fuchsia::driver::framework::NodeController> controller,
                fidl::ServerEnd<llcpp::fuchsia::driver::framework::Node> node,
                AddChildCompleter::Sync& completer) override;

  Node* const parent_;
  DriverBinder* const driver_binder_;
  async_dispatcher_t* const dispatcher_;

  const std::string name_;
  Offers offers_;
  Symbols symbols_;

  DriverHostComponent* driver_host_ = nullptr;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::component::runner::ComponentController>>
      driver_binding_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node>> node_binding_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::NodeController>>
      controller_binding_;
  fbl::DoublyLinkedList<std::unique_ptr<Node>> children_;
};

struct MatchResult {
  std::string url;
  llcpp::fuchsia::driver::framework::NodeAddArgs matched_args;
};

// TODO(fxbug.dev/33183): Replace this with a driver_index component.
class DriverIndex {
 public:
  using MatchCallback =
      fit::function<zx::status<MatchResult>(llcpp::fuchsia::driver::framework::NodeAddArgs args)>;
  explicit DriverIndex(MatchCallback match_callback);

  zx::status<MatchResult> Match(llcpp::fuchsia::driver::framework::NodeAddArgs args);

 private:
  MatchCallback match_callback_;
};

struct DriverArgs {
  fidl::ClientEnd<llcpp::fuchsia::io::Directory> exposed_dir;
  Node* node;
};

class DriverRunner : public llcpp::fuchsia::component::runner::ComponentRunner::Interface,
                     public DriverBinder {
 public:
  DriverRunner(fidl::ClientEnd<llcpp::fuchsia::sys2::Realm> realm, DriverIndex* driver_index,
               inspect::Inspector* inspector, async_dispatcher_t* dispatcher);

  fit::promise<inspect::Inspector> Inspect();
  zx::status<> PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> StartRootDriver(std::string_view name);

 private:
  // llcpp::fuchsia::component::runner::ComponentRunner::Interface
  void Start(llcpp::fuchsia::component::runner::ComponentStartInfo start_info,
             fidl::ServerEnd<llcpp::fuchsia::component::runner::ComponentController> controller,
             StartCompleter::Sync& completer) override;
  // DriverBinder
  zx::status<> Bind(Node* node, llcpp::fuchsia::driver::framework::NodeAddArgs args) override;

  zx::status<std::unique_ptr<DriverHostComponent>> StartDriverHost();
  zx::status<fidl::ClientEnd<llcpp::fuchsia::io::Directory>> CreateComponent(
      std::string name, std::string url, std::string collection);
  uint64_t NextId();

  uint64_t next_id_ = 0;
  fidl::Client<llcpp::fuchsia::sys2::Realm> realm_;
  DriverIndex* driver_index_;
  async_dispatcher_t* dispatcher_;
  Node root_node_;
  std::unordered_map<std::string, DriverArgs> driver_args_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverComponent>> drivers_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>> driver_hosts_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
