// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_RUNNER_H_

#include <fuchsia/component/runner/llcpp/fidl.h>
#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <fuchsia/sys2/llcpp/fidl.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>
#include <fs/pseudo_dir.h>

class DriverComponent : public llcpp::fuchsia::component::runner::ComponentController::Interface,
                        public fbl::DoublyLinkedListable<std::unique_ptr<DriverComponent>> {
 public:
  DriverComponent(zx::channel exposed_dir, zx::channel driver);

 private:
  // llcpp::fuchsia::component::runner::ComponentController::Interface
  void Stop(StopCompleter::Sync completer) override;
  void Kill(KillCompleter::Sync completer) override;

  zx::channel exposed_dir_;
  zx::channel driver_;
};

class DriverHostComponent : public fbl::DoublyLinkedListable<std::unique_ptr<DriverHostComponent>> {
 public:
  DriverHostComponent(zx::channel driver_host, async_dispatcher_t* dispatcher,
                      fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts);

  zx::status<zx::channel> Start(
      zx::channel node, llcpp::fuchsia::data::Dictionary program,
      fidl::VectorView<llcpp::fuchsia::component::runner::ComponentNamespaceEntry> ns,
      zx::channel outgoing_dir);

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
  Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher);
  ~Node() override;

  DriverHostComponent* parent_driver_host() const;
  void set_driver_host(DriverHostComponent* driver_host);
  void set_binding(fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node> binding);

  void Remove();

 private:
  // llcpp::fuchsia::driver::framework::NodeController::Interface
  void Remove(RemoveCompleter::Sync completer) override;
  // llcpp::fuchsia::driver::framework::Node::Interface
  void AddChild(llcpp::fuchsia::driver::framework::NodeAddArgs args, zx::channel controller,
                zx::channel node, AddChildCompleter::Sync completer) override;

  Node* parent_;
  DriverBinder* driver_binder_;
  async_dispatcher_t* dispatcher_;

  DriverHostComponent* driver_host_ = nullptr;
  fbl::DoublyLinkedList<std::unique_ptr<Node>> children_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Node>> binding_;
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
  zx::channel exposed_dir;
  Node* node;
};

class DriverRunner : public llcpp::fuchsia::component::runner::ComponentRunner::Interface,
                     public DriverBinder {
 public:
  DriverRunner(zx::channel realm, DriverIndex* driver_index, async_dispatcher_t* dispatcher);

  zx::status<> PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> StartRootDriver(std::string_view name);

 private:
  // llcpp::fuchsia::component::runner::ComponentRunner::Interface
  void Start(llcpp::fuchsia::component::runner::ComponentStartInfo start_info,
             zx::channel controller, StartCompleter::Sync completer) override;
  // DriverBinder
  zx::status<> Bind(Node* node, llcpp::fuchsia::driver::framework::NodeAddArgs args) override;

  zx::status<std::unique_ptr<DriverHostComponent>> StartDriverHost();
  zx::status<zx::channel> CreateComponent(std::string name, std::string url,
                                          std::string collection);
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
