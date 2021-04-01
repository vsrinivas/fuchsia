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

void InspectNode(inspect::Inspector* inspector, InspectStack* stack) {
  std::vector<inspect::Node> roots;
  while (!stack->empty()) {
    // Pop the current root and node to operate on.
    auto [root, node] = stack->top();
    stack->pop();

    // Populate root with data from node.
    if (auto offers = node->offers(); !offers.empty()) {
      std::vector<std::string_view> strings;
      for (auto& offer : offers) {
        strings.push_back(offer.get());
      }
      root->CreateString("offers", fxl::JoinStrings(strings, ", "), inspector);
    }
    if (auto symbols = node->symbols(); !symbols.empty()) {
      std::vector<std::string_view> strings;
      for (auto& symbol : symbols) {
        strings.push_back(symbol.name().get());
      }
      root->CreateString("symbols", fxl::JoinStrings(strings, ", "), inspector);
    }

    // Push children of this node onto the stack.
    for (auto& child : node->children()) {
      auto& root_for_child = roots.emplace_back(root->CreateChild(child.name()));
      stack->emplace(&root_for_child, &child);
    }
  }

  // Store all of the roots in the inspector.
  for (auto& root : roots) {
    inspector->emplace(std::move(root));
  }
}

template <typename T>
void UnbindAndReset(std::optional<fidl::ServerBindingRef<T>>& ref) {
  if (ref.has_value()) {
    ref->Unbind();
    ref.reset();
  }
}

std::string DriverCollection(std::string_view url) {
  constexpr auto scheme = "fuchsia-boot://";
  return url.compare(0, strlen(scheme), scheme) == 0 ? "boot-drivers" : "pkg-drivers";
}

}  // namespace

class EventHandler : public fdf::DriverHost::AsyncEventHandler {
 public:
  EventHandler(DriverHostComponent* component,
               fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
      : component_(component), driver_hosts_(driver_hosts) {}

  void Unbound(fidl::UnbindInfo info) override { driver_hosts_->erase(*component_); }

 private:
  DriverHostComponent* const component_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts_;
};

DriverComponent::DriverComponent(fidl::ClientEnd<fio::Directory> exposed_dir,
                                 fidl::ClientEnd<fdf::Driver> driver)
    : exposed_dir_(std::move(exposed_dir)),
      driver_(std::move(driver)),
      wait_(this, driver_.channel().get(), ZX_CHANNEL_PEER_CLOSED) {}

DriverComponent::~DriverComponent() { UnbindAndReset(node_ref_); }

void DriverComponent::set_driver_ref(
    fidl::ServerBindingRef<fuchsia_component_runner::ComponentController> driver_ref) {
  driver_ref_.emplace(std::move(driver_ref));
}

void DriverComponent::set_node_ref(fidl::ServerBindingRef<fdf::Node> node_ref) {
  node_ref_.emplace(std::move(node_ref));
}

zx::status<> DriverComponent::WatchDriver(async_dispatcher_t* dispatcher) {
  auto status = wait_.Begin(dispatcher);
  return zx::make_status(status);
}

void DriverComponent::Stop(DriverComponent::StopCompleter::Sync& completer) {
  UnbindAndReset(node_ref_);
}

void DriverComponent::Kill(DriverComponent::KillCompleter::Sync& completer) {}

void DriverComponent::OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    LOGF(WARNING, "Failed to watch channel for driver: %s", zx_status_get_string(status));
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
    LOGF(ERROR, "Failed to start driver '%s' in driver host: %s", binary.data(), start.error());
    return zx::error(start.status());
  }
  return zx::ok(std::move(endpoints->client));
}

Node::Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher,
           std::string_view name)
    : parent_(parent), driver_binder_(driver_binder), dispatcher_(dispatcher), name_(name) {}

Node::~Node() { UnbindAndReset(controller_ref_); }

const std::string& Node::name() const { return name_; }

fidl::VectorView<fidl::StringView> Node::offers() { return fidl::unowned_vec(offers_); }

fidl::VectorView<fdf::wire::NodeSymbol> Node::symbols() { return fidl::unowned_vec(symbols_); }

DriverHostComponent* Node::parent_driver_host() const { return parent_->driver_host_; }

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

fbl::DoublyLinkedList<std::unique_ptr<Node>>& Node::children() { return children_; }

std::string Node::TopoName() const {
  std::deque<std::string_view> names;
  for (auto node = this; node != nullptr; node = node->parent_) {
    names.push_front(node->name());
  }
  return fxl::JoinStrings(names, ".");
}

void Node::Unbind() {
  UnbindAndReset(driver_ref_);
  UnbindAndReset(controller_ref_);
  UnbindAndReset(node_ref_);
}

void Node::Remove() {
  // Create list of nodes to unbind.
  std::stack<Node*> stack{{this}};
  std::vector<Node*> nodes;
  std::unordered_set<Node*> unique_nodes;
  while (!stack.empty()) {
    auto node = stack.top();
    stack.pop();
    auto [_, inserted] = unique_nodes.emplace(node);
    if (!inserted) {
      // Only insert unique nodes from the DAG.
      continue;
    }
    nodes.emplace_back(node);
    for (auto& child : node->children_) {
      stack.emplace(&child);
    }
  }

  bool is_bound = node_ref_.has_value();
  // Traverse list of nodes in reverse order in order to unbind depth-first.
  // Note: Unbind() both unbinds and resets the optional server ref value. This
  // means that subsequent calls from children removing their own child nodes
  // are no-ops.
  for (auto node = nodes.rbegin(), end = nodes.rend(); node != end; ++node) {
    (*node)->Unbind();
  }
  if (parent_ != nullptr) {
    if (is_bound) {
      // Remove() is only called in response to a FIDL server being unbound.
      // This can happen implicitly, when the underlying channel is closed, or
      // explicitly, through a call to Remove() over FIDL (see method below).
      //
      // When this occurs, the node that FIDL is operating on contains a valid
      // server ref, and is therefore the root of a sub-tree being removed.
      //
      // To safely remove all of the children of the node, we need to tell all
      // FIDL servers to unbind themselves. However, this also means we need to
      // delay erasing of the root node of a sub-tree until all of its children
      // have erased themselves first, therefore avoiding a use-after-free.
      //
      // We can achieve this by delaying the removal of a node by posting it
      // onto the dispatcher, where all the unbind operations are occuring in
      // FIFO order.
      async::PostTask(dispatcher_, [this] { parent_->children_.erase(*this); });
    } else {
      // Otherwise, we are free to erase this node from its parent.
      parent_->children_.erase(*this);
    }
  }
}

void Node::Remove(RemoveCompleter::Sync& completer) {
  // When NodeController::Remove() is called, we unbind the Node. This causes
  // the Node server to then call Node::Remove().
  //
  // We take this approach to avoid a use-after-free, where calling
  // Node::Remove() directly would then cause the the Node server to do the
  // same, after the Node has already been freed.
  UnbindAndReset(node_ref_);
}

void Node::AddChild(fdf::wire::NodeAddArgs args, fidl::ServerEnd<fdf::NodeController> controller,
                    fidl::ServerEnd<fdf::Node> node, AddChildCompleter::Sync& completer) {
  if (!args.has_name()) {
    LOGF(ERROR, "Failed to add Node, a name must be provided");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  auto name = args.name().get();
  if (name.find('.') != std::string_view::npos) {
    LOGF(ERROR, "Failed to add Node '%.*s', name must not contain '.'", name.size(), name.data());
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  for (auto& child : children_) {
    if (child.name() == name) {
      LOGF(ERROR, "Failed to add Node '%.*s', names must be unique among siblings", name.size(),
           name.data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }
  auto child = std::make_unique<Node>(this, driver_binder_, dispatcher_, name);

  if (args.has_offers()) {
    child->offers_.reserve(args.offers().count());
    std::unordered_set<std::string_view> names;
    for (auto& offer : args.offers()) {
      auto inserted = names.emplace(offer.data(), offer.size()).second;
      if (!inserted) {
        LOGF(ERROR, "Failed to add Node '%.*s', duplicate offer '%.*s'", name.size(), name.data(),
             offer.size(), offer.data());
        completer.Close(ZX_ERR_INVALID_ARGS);
        return;
      }
      child->offers_.emplace_back(child->allocator_, offer.get());
    }
  }

  if (args.has_symbols()) {
    child->symbols_.reserve(args.symbols().count());
    std::unordered_set<std::string_view> names;
    for (auto& symbol : args.symbols()) {
      if (!symbol.has_name()) {
        LOGF(ERROR, "Failed to add Node '%.*s', a symbol is missing a name", name.size(),
             name.data());
        completer.Close(ZX_ERR_INVALID_ARGS);
        return;
      }
      if (!symbol.has_address()) {
        LOGF(ERROR, "Failed to add Node '%.*s', symbol '%.*s' is missing an address", name.size(),
             name.data(), symbol.name().size(), symbol.name().data());
        completer.Close(ZX_ERR_INVALID_ARGS);
        return;
      }
      auto inserted = names.emplace(symbol.name().data(), symbol.name().size()).second;
      if (!inserted) {
        LOGF(ERROR, "Failed to add Node '%.*s', duplicate symbol '%.*s'", name.size(), name.data(),
             symbol.name().size(), symbol.name().data());
        completer.Close(ZX_ERR_INVALID_ARGS);
        return;
      }
      fdf::wire::NodeSymbol node_symbol(child->allocator_);
      node_symbol.set_name(child->allocator_, child->allocator_, symbol.name().get());
      node_symbol.set_address(child->allocator_, symbol.address());
      child->symbols_.emplace_back(std::move(node_symbol));
    }
  }

  auto bind_controller = fidl::BindServer<fdf::NodeController::Interface>(
      dispatcher_, std::move(controller), child.get());
  if (bind_controller.is_error()) {
    LOGF(ERROR, "Failed to bind channel to NodeController '%.*s': %s", name.size(), name.data(),
         zx_status_get_string(bind_controller.error()));
    completer.Close(bind_controller.error());
    return;
  }
  child->set_controller_ref(bind_controller.take_value());

  if (node.is_valid()) {
    auto bind_node = fidl::BindServer<fdf::Node::Interface>(
        dispatcher_, std::move(node), child.get(),
        [](fdf::Node::Interface* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
    if (bind_node.is_error()) {
      LOGF(ERROR, "Failed to bind channel to Node '%.*s': %s", name.size(), name.data(),
           zx_status_get_string(bind_node.error()));
      completer.Close(bind_node.error());
      return;
    }
    child->set_node_ref(bind_node.take_value());
    children_.push_back(std::move(child));
  } else {
    auto child_ptr = child.get();
    auto callback = [this, child = std::move(child),
                     completer = completer.ToAsync()](zx::status<> result) mutable {
      if (result.is_error()) {
        completer.Close(result.status_value());
        return;
      }
      children_.push_back(std::move(child));
    };
    driver_binder_->Bind(child_ptr, std::move(args), std::move(callback));
  }
}

DriverRunner::DriverRunner(fidl::ClientEnd<fsys::Realm> realm,
                           fidl::ClientEnd<fuchsia_driver_framework::DriverIndex> driver_index,
                           inspect::Inspector* inspector, async_dispatcher_t* dispatcher)
    : realm_(std::move(realm), dispatcher),
      driver_index_(std::move(driver_index), dispatcher),
      dispatcher_(dispatcher),
      root_node_(nullptr, this, dispatcher, "root") {
  inspector->GetRoot().CreateLazyNode(
      "driver_runner", [this] { return Inspect(); }, inspector);
}

fit::promise<inspect::Inspector> DriverRunner::Inspect() {
  inspect::Inspector inspector;
  auto root = inspector.GetRoot().CreateChild(root_node_.name());
  InspectStack stack{{std::make_pair(&root, &root_node_)}};
  InspectNode(&inspector, &stack);
  inspector.emplace(std::move(root));
  return fit::make_ok_promise(inspector);
}

zx::status<> DriverRunner::PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](zx::channel request) {
    auto result = fidl::BindServer(dispatcher_, std::move(request), this);
    if (result.is_error()) {
      LOGF(ERROR, "Failed to bind channel to '%s': %s", frunner::ComponentRunner::Name,
           zx_status_get_string(result.error()));
      return result.error();
    }
    return ZX_OK;
  };
  zx_status_t status =
      svc_dir->AddEntry(frunner::ComponentRunner::Name, fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", frunner::ComponentRunner::Name,
         zx_status_get_string(status));
  }
  return zx::make_status(status);
}

zx::status<> DriverRunner::StartRootDriver(std::string_view url) {
  return StartDriver(&root_node_, url);
}

zx::status<> DriverRunner::StartDriver(Node* node, std::string_view url) {
  auto create_result = CreateComponent(node->TopoName(), std::string(url), DriverCollection(url));
  if (create_result.is_error()) {
    return create_result.take_error();
  }
  driver_args_.emplace(url, DriverArgs{std::move(create_result.value()), node});
  return zx::ok();
}

void DriverRunner::Start(frunner::wire::ComponentStartInfo start_info,
                         fidl::ServerEnd<frunner::ComponentController> controller,
                         StartCompleter::Sync& completer) {
  std::string url(start_info.resolved_url().get());
  auto it = driver_args_.find(url);
  if (it == driver_args_.end()) {
    LOGF(ERROR, "Failed to start driver '%.*s', unknown request for driver", url.size(),
         url.data());
    completer.Close(ZX_ERR_UNAVAILABLE);
    return;
  }
  auto driver_args = std::move(it->second);
  driver_args_.erase(it);
  auto symbols = driver_args.node->symbols();

  // Launch a driver host, or use an existing driver host.
  DriverHostComponent* driver_host;
  if (start_args::ProgramValue(start_info.program(), "colocate").value_or("") == "true") {
    if (driver_args.node == &root_node_) {
      LOGF(ERROR, "Failed to start driver '%.*s', root driver cannot colocate", url.size(),
           url.data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    driver_host = driver_args.node->parent_driver_host();
  } else {
    // Do not pass symbols across driver hosts.
    symbols.set_count(0);

    auto result = StartDriverHost();
    if (result.is_error()) {
      completer.Close(result.error_value());
      return;
    }
    driver_host = result.value().get();
    driver_hosts_.push_back(std::move(result.value()));
  }
  driver_args.node->set_driver_host(driver_host);

  // Start the driver within the driver host.
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();
  if (endpoints.is_error()) {
    completer.Close(endpoints.status_value());
    return;
  }
  auto exposed_dir = service::Clone(driver_args.exposed_dir);
  if (!exposed_dir.is_ok()) {
    LOGF(ERROR, "Failed to clone exposed directory for driver '%.*s': %s", url.size(), url.data(),
         exposed_dir.status_string());
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }
  auto start = driver_host->Start(std::move(endpoints->client), driver_args.node->offers(),
                                  std::move(symbols), std::move(start_info.resolved_url()),
                                  std::move(start_info.program()), std::move(start_info.ns()),
                                  std::move(start_info.outgoing_dir()), std::move(*exposed_dir));
  if (start.is_error()) {
    completer.Close(start.error_value());
    return;
  }

  // Create a DriverComponent to manage the driver.
  auto driver = std::make_unique<DriverComponent>(std::move(driver_args.exposed_dir),
                                                  std::move(start.value()));
  auto bind_driver = fidl::BindServer<DriverComponent>(
      dispatcher_, std::move(controller), driver.get(),
      [this](DriverComponent* driver, auto, auto) { drivers_.erase(*driver); });
  if (bind_driver.is_error()) {
    LOGF(ERROR, "Failed to bind channel to ComponentController for driver '%.*s': %s", url.size(),
         url.data(), zx_status_get_string(bind_driver.error()));
    completer.Close(bind_driver.error());
    return;
  }
  driver_args.node->set_driver_ref(bind_driver.value());
  driver->set_driver_ref(bind_driver.take_value());
  auto watch = driver->WatchDriver(dispatcher_);
  if (watch.is_error()) {
    LOGF(ERROR, "Failed to watch channel for driver '%.*s': %s", url.size(), url.data(),
         watch.status_string());
    completer.Close(watch.error_value());
    return;
  }

  // Bind the Node associated with the driver.
  auto bind_node = fidl::BindServer<fdf::Node::Interface>(
      dispatcher_, std::move(endpoints->server), driver_args.node,
      [](fdf::Node::Interface* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
  if (bind_node.is_error()) {
    LOGF(ERROR, "Failed to bind channel to Node for driver '%.*s': %s", url.size(), url.data(),
         zx_status_get_string(bind_node.error()));
    completer.Close(bind_node.error());
    return;
  }
  driver_args.node->set_node_ref(bind_node.value());
  driver->set_node_ref(bind_node.take_value());
  drivers_.push_back(std::move(driver));
}

void DriverRunner::Bind(Node* node, fdf::wire::NodeAddArgs args,
                        fit::callback<void(zx::status<>)> callback) {
  auto match_callback = [this, callback = callback.share(),
                         node](fdf::DriverIndex::MatchDriverResponse* response) mutable {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to match driver %s: %s", node->name().data(),
           zx_status_get_string(response->result.err()));
      callback(zx::error(response->result.err()));
      return;
    }
    auto& url = response->result.response().url;
    auto start_result = StartDriver(node, url.get());
    if (start_result.is_error()) {
      LOGF(ERROR, "Failed to start driver %s: %s", node->name().data(),
           zx_status_get_string(start_result.error_value()));
      callback(start_result.take_error());
      return;
    }
    callback(zx::ok());
  };
  auto match_result = driver_index_->MatchDriver(std::move(args), std::move(match_callback));
  if (!match_result.ok()) {
    LOGF(ERROR, "Failed to call match driver %s: %s", node->name().data(), match_result.error());
    callback(zx::error(match_result.status()));
  }
}

zx::status<std::unique_ptr<DriverHostComponent>> DriverRunner::StartDriverHost() {
  auto name = "driver_host-" + std::to_string(next_driver_host_id_++);
  auto create = CreateComponent(name, "fuchsia-boot:///#meta/driver_host2.cm", "driver_hosts");
  if (create.is_error()) {
    return create.take_error();
  }

  auto endpoints = fidl::CreateEndpoints<fdf::DriverHost>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  zx_status_t status = fdio_service_connect_at(create->channel().get(), fdf::DriverHost::Name,
                                               endpoints->server.TakeChannel().release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to connect to service '%s': %s", fdf::DriverHost::Name,
         zx_status_get_string(status));
    return zx::error(status);
  }

  auto driver_host = std::make_unique<DriverHostComponent>(std::move(endpoints->client),
                                                           dispatcher_, &driver_hosts_);
  return zx::ok(std::move(driver_host));
}

zx::status<fidl::ClientEnd<fio::Directory>> DriverRunner::CreateComponent(std::string name,
                                                                          std::string url,
                                                                          std::string collection) {
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto bind_callback = [name](fsys::Realm::BindChildResponse* response) {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to bind component '%s': %u", name.data(), response->result.err());
    }
  };
  auto create_callback = [this, name, collection, server_end = std::move(endpoints->server),
                          bind_callback = std::move(bind_callback)](
                             fsys::Realm::CreateChildResponse* response) mutable {
    if (response->result.is_err()) {
      LOGF(ERROR, "Failed to create component '%s': %u", name.data(), response->result.err());
      return;
    }
    auto bind = realm_->BindChild(fsys::wire::ChildRef{.name = fidl::unowned_str(name),
                                                       .collection = fidl::unowned_str(collection)},
                                  std::move(server_end), std::move(bind_callback));
    if (!bind.ok()) {
      LOGF(ERROR, "Failed to bind component '%s': %s", name.data(), bind.error());
    }
  };
  fidl::FidlAllocator allocator;
  fsys::wire::ChildDecl child_decl(allocator);
  child_decl.set_name(allocator, fidl::unowned_str(name))
      .set_url(allocator, fidl::unowned_str(url))
      .set_startup(allocator, fsys::wire::StartupMode::LAZY);
  auto create =
      realm_->CreateChild(fsys::wire::CollectionRef{.name = fidl::unowned_str(collection)},
                          std::move(child_decl), std::move(create_callback));
  if (!create.ok()) {
    LOGF(ERROR, "Failed to create component '%s': %s", name.data(), create.error());
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::move(endpoints->client));
}
