// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_runner.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/service/llcpp/service.h>
#include <zircon/status.h>

#include <deque>
#include <stack>
#include <unordered_set>

#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace frunner = fuchsia_component_runner;
namespace fsys = fuchsia_sys2;

using InspectStack = std::stack<std::pair<inspect::Node*, Node*>>;

namespace {

void InspectNode(inspect::Inspector& inspector, InspectStack& stack) {
  std::vector<inspect::Node> roots;
  while (!stack.empty()) {
    // Pop the current root and node to operate on.
    auto [root, node] = stack.top();
    stack.pop();

    // Populate root with data from node.
    if (auto offers = node->offers(); !offers.empty()) {
      std::vector<std::string_view> strings;
      for (auto& offer : offers) {
        strings.push_back(offer.get());
      }
      root->CreateString("offers", fxl::JoinStrings(strings, ", "), &inspector);
    }
    if (auto symbols = node->symbols(); !symbols.empty()) {
      std::vector<std::string_view> strings;
      for (auto& symbol : symbols) {
        strings.push_back(symbol.name().get());
      }
      root->CreateString("symbols", fxl::JoinStrings(strings, ", "), &inspector);
    }

    // Push children of this node onto the stack.
    for (auto& child : node->children()) {
      auto& root_for_child = roots.emplace_back(root->CreateChild(child->name()));
      stack.emplace(&root_for_child, child.get());
    }
  }

  // Store all of the roots in the inspector.
  for (auto& root : roots) {
    inspector.emplace(std::move(root));
  }
}

std::string DriverCollection(std::string_view url) {
  constexpr auto scheme = "fuchsia-boot://";
  return url.compare(0, strlen(scheme), scheme) == 0 ? "boot-drivers" : "pkg-drivers";
}

Node* PrimaryParent(const std::vector<Node*>& parents) {
  return parents.empty() ? nullptr : parents[0];
}

template <typename T>
bool UnbindAndReset(std::optional<fidl::ServerBindingRef<T>>& ref) {
  bool unbind = ref.has_value();
  if (unbind) {
    ref->Unbind();
    ref.reset();
  }
  return unbind;
}

}  // namespace

// Holds the state of removal operation in the driver topology.
//
// Removal is complicated by the fact that we need to wait for drivers that are
// bound to nodes to stop before we continue remove nodes. Otherwise, a parent
// driver can be removed before a child driver, and cause system instability.
//
// Removal of a node happens in the following steps:
// 1. The FIDL server detects the client has unbound the node.
// 2. Node::Remove() is called on the node.
//    a. If an AsyncRemove is stored in the node, we jump to step 5.
//    b. Otherwise, we continue to step 3.
// 3. The node is removed from its parent.
// 4. A list of the nodes children is created, ordered by depth.
// 5. AsyncRemove::Continue() is called to remove each node, depth-first.
// 6. When a node is removed, if it was bound to a driver, we:
//    a. Unbind the driver. This causes the DriverComponent to be destroyed,
//       which closes the associated Driver channel. When the Driver channel is
//       closed, this informs the driver host (that is hosting the driver) to
//       stop the driver.
//    b. Store the AsyncRemove within the node.
//    c. Stop removing any further nodes.
//    d. Go back to step 1.
// 7. Delete that final node at the root of the removal operation.
class AsyncRemove {
 public:
  AsyncRemove(std::shared_ptr<Node> root, std::vector<Node*> nodes)
      : root_(std::move(root)), nodes_(std::move(nodes)) {}

  void Continue(std::unique_ptr<AsyncRemove> self) {
    // To unbind nodes depth-first, we traverse the list in reverse.
    auto node = nodes_.rbegin();
    for (auto end = nodes_.rend(); node != end; ++node) {
      // If unbind returns true, a node has taken ownership of the async remove
      // and is waiting for the driver bound to it to stop. We must halt removal
      // and wait to be called again.
      if ((*node)->Unbind(self)) {
        break;
      }
    }
    // Erase all of removed nodes.
    nodes_.erase(node.base(), nodes_.end());
    if (nodes_.size() == 1) {
      // Only the root is left, so delete it.
      root_.reset();
    }
  }

 private:
  // In `root_`, we store root of the sub-tree we are removing. This ensures
  // that the pointers stored in `nodes_` is valid for the life-time of `this`.
  std::shared_ptr<Node> root_;
  std::vector<Node*> nodes_;
};

class EventHandler : public fidl::WireAsyncEventHandler<fdf::DriverHost> {
 public:
  EventHandler(DriverHostComponent* component,
               fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
      : component_(component), driver_hosts_(driver_hosts) {}

  void Unbound(fidl::UnbindInfo info) override { driver_hosts_->erase(*component_); }

 private:
  DriverHostComponent* const component_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts_;
};

DriverComponent::DriverComponent(fidl::ClientEnd<fdf::Driver> driver)
    : driver_(std::move(driver)), wait_(this, driver_.channel().get(), ZX_CHANNEL_PEER_CLOSED) {}

void DriverComponent::set_driver_ref(
    fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref) {
  driver_ref_.emplace(std::move(driver_ref));
}

zx::status<> DriverComponent::Watch(async_dispatcher_t* dispatcher) {
  zx_status_t status = wait_.Begin(dispatcher);
  return zx::make_status(status);
}

void DriverComponent::Stop(StopRequestView request,
                           DriverComponent::StopCompleter::Sync& completer) {
  zx_status_t status = wait_.Cancel();
  if (status != ZX_OK) {
    LOGF(WARNING, "Failed to cancel watch on driver: %s", zx_status_get_string(status));
  }
  driver_.reset();
}

void DriverComponent::Kill(KillRequestView request,
                           DriverComponent::KillCompleter::Sync& completer) {}

void DriverComponent::OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    LOGF(WARNING, "Failed to watch driver: %s", zx_status_get_string(status));
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    UnbindAndReset(driver_ref_);
  }
}

DriverHostComponent::DriverHostComponent(
    fidl::ClientEnd<fdf::DriverHost> driver_host, async_dispatcher_t* dispatcher,
    fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
    : driver_host_(std::move(driver_host), dispatcher,
                   std::make_shared<EventHandler>(this, driver_hosts)) {}

zx::status<fidl::ClientEnd<fdf::Driver>> DriverHostComponent::Start(
    fidl::ClientEnd<fdf::Node> node, fidl::VectorView<fidl::StringView> offers,
    fidl::VectorView<fdf::wire::NodeSymbol> symbols, fidl::StringView url,
    fdata::wire::Dictionary program, fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns,
    fidl::ServerEnd<fio::Directory> outgoing_dir, fidl::ClientEnd<fio::Directory> exposed_dir) {
  auto endpoints = fidl::CreateEndpoints<fdf::Driver>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  fidl::FidlAllocator allocator;
  fdf::wire::DriverStartArgs args(allocator);
  args.set_node(allocator, std::move(node))
      .set_offers(allocator, std::move(offers))
      .set_symbols(allocator, std::move(symbols))
      .set_url(allocator, std::move(url))
      .set_program(allocator, std::move(program))
      .set_ns(allocator, std::move(ns))
      .set_outgoing_dir(allocator, std::move(outgoing_dir))
      .set_exposed_dir(allocator, std::move(exposed_dir));
  auto start = driver_host_->Start(std::move(args), std::move(endpoints->server));
  if (!start.ok()) {
    auto binary = start_args::ProgramValue(program, "binary").value_or("");
    LOGF(ERROR, "Failed to start driver '%s' in driver host: %s", binary.data(),
         start.error_message());
    return zx::error(start.status());
  }
  return zx::ok(std::move(endpoints->client));
}

Node::Node(std::vector<Node*> parents, DriverBinder* driver_binder, async_dispatcher_t* dispatcher,
           std::string_view name)
    : parents_(std::move(parents)),
      driver_binder_(driver_binder),
      dispatcher_(dispatcher),
      name_(name) {}

Node::~Node() { UnbindAndReset(controller_ref_); }

const std::string& Node::name() const { return name_; }

fidl::VectorView<fidl::StringView> Node::offers() {
  return fidl::VectorView<fidl::StringView>::FromExternal(offers_);
}

fidl::VectorView<fdf::wire::NodeSymbol> Node::symbols() {
  return fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(symbols_);
}

DriverHostComponent* Node::parent_driver_host() const {
  return *PrimaryParent(parents_)->driver_host_;
}

void Node::set_driver_host(DriverHostComponent* driver_host) { driver_host_ = driver_host; }

void Node::set_driver_ref(fidl::ServerBindingRef<frunner::ComponentController> driver_ref) {
  driver_ref_.emplace(std::move(driver_ref));
}

void Node::set_controller_ref(fidl::ServerBindingRef<fdf::NodeController> controller_ref) {
  controller_ref_.emplace(std::move(controller_ref));
}

void Node::set_node_ref(fidl::ServerBindingRef<fdf::Node> node_ref) {
  node_ref_.emplace(std::move(node_ref));
}

const std::vector<std::shared_ptr<Node>>& Node::children() const { return children_; }

std::string Node::TopoName() const {
  std::deque<std::string_view> names;
  for (auto node = this; node != nullptr; node = PrimaryParent(node->parents_)) {
    names.push_front(node->name());
  }
  return fxl::JoinStrings(names, ".");
}

bool Node::Unbind(std::unique_ptr<AsyncRemove>& async_remove) {
  bool has_driver = UnbindAndReset(driver_ref_);
  if (has_driver) {
    // If a driver was bound to this node, store the async remove and wait for
    // the driver to stop.
    async_remove_ = std::move(async_remove);
  } else {
    // Otherwise, unbind the controller and node servers.
    UnbindAndReset(controller_ref_);
    UnbindAndReset(node_ref_);
  }
  return has_driver;
}

void Node::Remove() {
  // If we were waiting for a driver to stop, continue removal from where we
  // left off.
  if (async_remove_) {
    async_remove_->Continue(std::move(async_remove_));
    return;
  }

  // Remove this node from its parent.
  std::shared_ptr<Node> this_node;
  if (!parents_.empty()) {
    this_node = shared_from_this();
    for (auto parent : parents_) {
      auto& children = parent->children_;
      children.erase(std::find(children.begin(), children.end(), this_node));
    }
  }

  // Create list of nodes to unbind.
  std::stack<Node*> stack{{this}};
  std::vector<Node*> nodes;
  std::unordered_set<Node*> unique_nodes;
  while (!stack.empty()) {
    auto node = stack.top();
    stack.pop();
    auto [_, inserted] = unique_nodes.insert(node);
    if (!inserted) {
      // Only insert unique nodes from the DAG.
      continue;
    }
    // Disable driver binding for the node. This also prevents child nodes from
    // being added to this node.
    node->driver_binder_ = nullptr;
    nodes.push_back(node);
    for (auto& child : node->children_) {
      stack.push(child.get());
    }
  }

  // Begin removal of this node and its children.
  auto new_remove = std::make_unique<AsyncRemove>(std::move(this_node), std::move(nodes));
  new_remove->Continue(std::move(new_remove));
}

void Node::Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) {
  // When NodeController::Remove() is called, we unbind the Node. This causes
  // the Node server to then call Node::Remove().
  //
  // We take this approach to avoid a use-after-free, where calling
  // Node::Remove() directly would then cause the the Node server to do the
  // same, after the Node has already been freed.
  UnbindAndReset(node_ref_);
}

void Node::AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) {
  if (driver_binder_ == nullptr) {
    LOGF(ERROR, "Failed to add Node, as this Node '%s' was removed", name_.data());
    completer.ReplyError(fdf::wire::NodeError::kNodeRemoved);
    return;
  }
  if (!request->args.has_name()) {
    LOGF(ERROR, "Failed to add Node, a name must be provided");
    completer.ReplyError(fdf::wire::NodeError::kNameMissing);
    return;
  }
  auto name = request->args.name().get();
  if (name.find('.') != std::string_view::npos) {
    LOGF(ERROR, "Failed to add Node '%.*s', name must not contain '.'", name.size(), name.data());
    completer.ReplyError(fdf::wire::NodeError::kNameInvalid);
    return;
  }
  for (auto& child : children_) {
    if (child->name() == name) {
      LOGF(ERROR, "Failed to add Node '%.*s', name already exists among siblings", name.size(),
           name.data());
      completer.ReplyError(fdf::wire::NodeError::kNameAlreadyExists);
      return;
    }
  };
  std::vector<Node*> parents{this};
  auto child = std::make_shared<Node>(std::move(parents), *driver_binder_, dispatcher_, name);

  if (request->args.has_offers()) {
    child->offers_.reserve(request->args.offers().count());
    std::unordered_set<std::string_view> names;
    for (auto& offer : request->args.offers()) {
      auto inserted = names.emplace(offer.data(), offer.size()).second;
      if (!inserted) {
        LOGF(ERROR, "Failed to add Node '%.*s', offer '%.*s' already exists", name.size(),
             name.data(), offer.size(), offer.data());
        completer.ReplyError(fdf::wire::NodeError::kOfferAlreadyExists);
        return;
      }
      child->offers_.emplace_back(child->allocator_, offer.get());
    }
  }

  if (request->args.has_symbols()) {
    child->symbols_.reserve(request->args.symbols().count());
    std::unordered_set<std::string_view> names;
    for (auto& symbol : request->args.symbols()) {
      if (!symbol.has_name()) {
        LOGF(ERROR, "Failed to add Node '%.*s', a symbol is missing a name", name.size(),
             name.data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolNameMissing);
        return;
      }
      if (!symbol.has_address()) {
        LOGF(ERROR, "Failed to add Node '%.*s', symbol '%.*s' is missing an address", name.size(),
             name.data(), symbol.name().size(), symbol.name().data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolAddressMissing);
        return;
      }
      auto inserted = names.emplace(symbol.name().data(), symbol.name().size()).second;
      if (!inserted) {
        LOGF(ERROR, "Failed to add Node '%.*s', symbol '%.*s' already exists", name.size(),
             name.data(), symbol.name().size(), symbol.name().data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolAlreadyExists);
        return;
      }
      fdf::wire::NodeSymbol node_symbol(child->allocator_);
      node_symbol.set_name(child->allocator_, child->allocator_, symbol.name().get());
      node_symbol.set_address(child->allocator_, symbol.address());
      child->symbols_.emplace_back(std::move(node_symbol));
    }
  }

  auto bind_controller = fidl::BindServer<fidl::WireServer<fdf::NodeController>>(
      dispatcher_, std::move(request->controller), child.get());
  child->set_controller_ref(std::move(bind_controller));

  if (request->node.is_valid()) {
    auto bind_node = fidl::BindServer<fidl::WireServer<fdf::Node>>(
        dispatcher_, std::move(request->node), child.get(),
        [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
    child->set_node_ref(std::move(bind_node));
    children_.push_back(std::move(child));
    completer.ReplySuccess();
  } else {
    auto child_ptr = child.get();
    auto callback = [this, child = std::move(child),
                     completer = completer.ToAsync()](zx::status<> result) mutable {
      if (result.is_error()) {
        completer.Close(result.status_value());
        return;
      }
      children_.push_back(std::move(child));
      completer.ReplySuccess();
    };
    (*driver_binder_)->Bind(*child_ptr, std::move(request->args), std::move(callback));
  }
}

DriverRunner::DriverRunner(fidl::ClientEnd<fsys::Realm> realm,
                           fidl::ClientEnd<fuchsia_driver_framework::DriverIndex> driver_index,
                           inspect::Inspector& inspector, async_dispatcher_t* dispatcher)
    : realm_(std::move(realm), dispatcher),
      driver_index_(std::move(driver_index), dispatcher),
      dispatcher_(dispatcher),
      root_node_({}, this, dispatcher, "root") {
  inspector.GetRoot().CreateLazyNode(
      "driver_runner", [this] { return Inspect(); }, &inspector);
}

fit::promise<inspect::Inspector> DriverRunner::Inspect() {
  inspect::Inspector inspector;
  auto root = inspector.GetRoot().CreateChild(root_node_.name());
  InspectStack stack{{std::make_pair(&root, &root_node_)}};
  InspectNode(inspector, stack);
  inspector.emplace(std::move(root));
  return fit::make_ok_promise(inspector);
}

zx::status<> DriverRunner::PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<frunner::ComponentRunner> request) {
    fidl::BindServer(dispatcher_, std::move(request), this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<frunner::ComponentRunner>,
                                         fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s",
         fidl::DiscoverableProtocolName<frunner::ComponentRunner>, zx_status_get_string(status));
  }
  return zx::make_status(status);
}

zx::status<> DriverRunner::StartRootDriver(std::string_view url) {
  return StartDriver(root_node_, url);
}

zx::status<> DriverRunner::StartDriver(Node& node, std::string_view url) {
  auto create_result = CreateComponent(node.TopoName(), std::string(url), DriverCollection(url));
  if (create_result.is_error()) {
    return create_result.take_error();
  }
  driver_args_.emplace(url, DriverArgs{std::move(*create_result), node});
  return zx::ok();
}

void DriverRunner::Start(StartRequestView request, StartCompleter::Sync& completer) {
  std::string url(request->start_info.resolved_url().get());
  auto it = driver_args_.find(url);
  if (it == driver_args_.end()) {
    LOGF(ERROR, "Failed to start driver '%.*s', unknown request for driver", url.size(),
         url.data());
    completer.Close(ZX_ERR_UNAVAILABLE);
    return;
  }
  auto driver_args = std::move(it->second);
  driver_args_.erase(it);
  auto symbols = driver_args.node.symbols();

  // Launch a driver host, or use an existing driver host.
  DriverHostComponent* driver_host;
  if (start_args::ProgramValue(request->start_info.program(), "colocate").value_or("") == "true") {
    if (&driver_args.node == &root_node_) {
      LOGF(ERROR, "Failed to start driver '%.*s', root driver cannot colocate", url.size(),
           url.data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    driver_host = driver_args.node.parent_driver_host();
  } else {
    // Do not pass symbols across driver hosts.
    symbols.set_count(0);

    auto result = StartDriverHost();
    if (result.is_error()) {
      completer.Close(result.error_value());
      return;
    }
    driver_host = result.value().get();
    driver_hosts_.push_back(std::move(*result));
  }
  driver_args.node.set_driver_host(driver_host);

  // Bind the Node associated with the driver.
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();
  if (endpoints.is_error()) {
    completer.Close(endpoints.status_value());
    return;
  }
  auto bind_node = fidl::BindServer<fidl::WireServer<fdf::Node>>(
      dispatcher_, std::move(endpoints->server), &driver_args.node,
      [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
  driver_args.node.set_node_ref(bind_node);

  // Start the driver within the driver host.
  auto start = driver_host->Start(
      std::move(endpoints->client), driver_args.node.offers(), std::move(symbols),
      std::move(request->start_info.resolved_url()), std::move(request->start_info.program()),
      std::move(request->start_info.ns()), std::move(request->start_info.outgoing_dir()),
      std::move(driver_args.exposed_dir));
  if (start.is_error()) {
    completer.Close(start.error_value());
    return;
  }

  // Create a DriverComponent to manage the driver.
  auto driver = std::make_unique<DriverComponent>(std::move(*start));
  auto bind_driver = fidl::BindServer<DriverComponent>(
      dispatcher_, std::move(request->controller), driver.get(),
      [this, name = driver_args.node.TopoName(), collection = DriverCollection(url)](
          DriverComponent* driver, auto, auto) {
        drivers_.erase(*driver);
        auto destroy_callback = [name](fidl::WireResponse<fsys::Realm::DestroyChild>* response) {
          if (response->result.is_err()) {
            LOGF(ERROR, "Failed to destroy component '%s': %u", name.data(),
                 response->result.err());
          }
        };
        auto destroy = realm_->DestroyChild(
            fsys::wire::ChildRef{.name = fidl::StringView::FromExternal(name),
                                 .collection = fidl::StringView::FromExternal(collection)},
            std::move(destroy_callback));
        if (!destroy.ok()) {
          LOGF(ERROR, "Failed to destroy component '%s': %s", name.data(), destroy.error_message());
        }
      });
  driver_args.node.set_driver_ref(bind_driver);
  driver->set_driver_ref(std::move(bind_driver));
  auto watch = driver->Watch(dispatcher_);
  if (watch.is_error()) {
    LOGF(ERROR, "Failed to watch channel for driver '%.*s': %s", url.size(), url.data(),
         watch.status_string());
    completer.Close(watch.error_value());
    return;
  }
  drivers_.push_back(std::move(driver));
}

void DriverRunner::Bind(Node& node, fdf::wire::NodeAddArgs args,
                        fit::callback<void(zx::status<>)> callback) {
  auto match_callback = [this, callback = callback.share(), &node](
                            fidl::WireResponse<fdf::DriverIndex::MatchDriver>* response) mutable {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to match driver '%s': %s", node.name().data(),
           zx_status_get_string(response->result.err()));
      callback(zx::error(response->result.err()));
      return;
    }
    auto& matched_driver = response->result.response().driver;
    if (!matched_driver.has_url()) {
      LOGF(ERROR, "Failed to match driver '%s', driver URL is missing", node.name().data());
      callback(zx::error(ZX_ERR_INVALID_ARGS));
      return;
    }
    auto start_result = StartDriver(node, matched_driver.url().get());
    if (start_result.is_error()) {
      LOGF(ERROR, "Failed to start driver '%s': %s", node.name().data(),
           zx_status_get_string(start_result.error_value()));
      callback(start_result.take_error());
      return;
    }
    callback(zx::ok());
  };
  auto match_result = driver_index_->MatchDriver(std::move(args), std::move(match_callback));
  if (!match_result.ok()) {
    LOGF(ERROR, "Failed to call match driver '%s': %s", node.name().data(),
         match_result.error_message());
    callback(zx::error(match_result.status()));
  }
}

zx::status<std::unique_ptr<DriverHostComponent>> DriverRunner::StartDriverHost() {
  auto name = "driver_host-" + std::to_string(next_driver_host_id_++);
  auto create = CreateComponent(name, "fuchsia-boot:///#meta/driver_host2.cm", "driver_hosts");
  if (create.is_error()) {
    return create.take_error();
  }

  auto client_end = service::ConnectAt<fdf::DriverHost>(*create);
  if (client_end.is_error()) {
    LOGF(ERROR, "Failed to connect to service '%s': %s",
         fidl::DiscoverableProtocolName<fdf::DriverHost>, client_end.status_string());
    return client_end.take_error();
  }

  auto driver_host =
      std::make_unique<DriverHostComponent>(std::move(*client_end), dispatcher_, &driver_hosts_);
  return zx::ok(std::move(driver_host));
}

zx::status<fidl::ClientEnd<fio::Directory>> DriverRunner::CreateComponent(std::string name,
                                                                          std::string url,
                                                                          std::string collection) {
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto bind_callback = [name](fidl::WireResponse<fsys::Realm::BindChild>* response) {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to bind component '%s': %u", name.data(), response->result.err());
    }
  };
  auto create_callback = [this, name, collection, server_end = std::move(endpoints->server),
                          bind_callback = std::move(bind_callback)](
                             fidl::WireResponse<fsys::Realm::CreateChild>* response) mutable {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to create component '%s': %u", name.data(), response->result.err());
      return;
    }
    auto bind = realm_->BindChild(
        fsys::wire::ChildRef{.name = fidl::StringView::FromExternal(name),
                             .collection = fidl::StringView::FromExternal(collection)},
        std::move(server_end), std::move(bind_callback));
    if (!bind.ok()) {
      LOGF(ERROR, "Failed to bind component '%s': %s", name.data(), bind.error_message());
    }
  };
  fidl::FidlAllocator allocator;
  fsys::wire::ChildDecl child_decl(allocator);
  child_decl.set_name(allocator, fidl::StringView::FromExternal(name))
      .set_url(allocator, fidl::StringView::FromExternal(url))
      .set_startup(allocator, fsys::wire::StartupMode::kLazy);
  auto create = realm_->CreateChild(
      fsys::wire::CollectionRef{.name = fidl::StringView::FromExternal(collection)},
      std::move(child_decl), std::move(create_callback));
  if (!create.ok()) {
    LOGF(ERROR, "Failed to create component '%s': %s", name.data(), create.error_message());
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::move(endpoints->client));
}
