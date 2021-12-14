// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_runner.h"

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.process/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/service/llcpp/service.h>
#include <zircon/status.h>

#include <deque>
#include <forward_list>
#include <stack>
#include <unordered_set>

#include "src/devices/lib/driver2/start_args.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdata = fuchsia_data;
namespace fdf = fuchsia_driver_framework;
namespace fio = fuchsia_io;
namespace fprocess = fuchsia_process;
namespace frunner = fuchsia_component_runner;
namespace fcomponent = fuchsia_component;
namespace fdecl = fuchsia_component_decl;

using InspectStack = std::stack<std::pair<inspect::Node*, const Node*>>;

namespace {

constexpr uint32_t kTokenId = PA_HND(PA_USER0, 0);
constexpr auto kBootScheme = "fuchsia-boot://";

template <typename R, typename F>
std::optional<R> VisitOffer(fdecl::wire::Offer& offer, F apply) {
  // Note, we access each field of the union as mutable, so that `apply` can
  // modify the field if necessary.
  switch (offer.which()) {
    case fdecl::wire::Offer::Tag::kService:
      return apply(offer.mutable_service());
    case fdecl::wire::Offer::Tag::kProtocol:
      return apply(offer.mutable_protocol());
    case fdecl::wire::Offer::Tag::kDirectory:
      return apply(offer.mutable_directory());
    case fdecl::wire::Offer::Tag::kStorage:
      return apply(offer.mutable_storage());
    case fdecl::wire::Offer::Tag::kRunner:
      return apply(offer.mutable_runner());
    case fdecl::wire::Offer::Tag::kResolver:
      return apply(offer.mutable_resolver());
    case fdecl::wire::Offer::Tag::kEvent:
      return apply(offer.mutable_event());
    case fdecl::wire::Offer::Tag::kUnknown:
      return {};
  }
}

void InspectNode(inspect::Inspector& inspector, InspectStack& stack) {
  const auto inspect_decl = [](auto& decl) -> std::string_view {
    if (decl.has_target_name()) {
      return decl.target_name().get();
    }
    if (decl.has_source_name()) {
      return decl.source_name().get();
    }
    return "<missing>";
  };

  std::forward_list<inspect::Node> roots;
  std::unordered_set<const Node*> unique_nodes;
  while (!stack.empty()) {
    // Pop the current root and node to operate on.
    auto [root, node] = stack.top();
    stack.pop();

    auto [_, inserted] = unique_nodes.insert(node);
    if (!inserted) {
      // Only insert unique nodes from the DAG.
      continue;
    }

    // Populate root with data from node.
    if (auto& offers = node->offers(); !offers.empty()) {
      std::vector<std::string_view> strings;
      for (auto& offer : offers) {
        auto string = VisitOffer<std::string_view>(offer->get(), inspect_decl);
        strings.push_back(string.value_or("unknown"));
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

    // Push children of this node onto the stack. We do this in reverse order to
    // ensure the children are handled in order, from first to last.
    auto& children = node->children();
    for (auto child = children.rbegin(), end = children.rend(); child != end; ++child) {
      auto& name = (*child)->name();
      auto& root_for_child = roots.emplace_front(root->CreateChild(name));
      stack.emplace(&root_for_child, child->get());
    }
  }

  // Store all of the roots in the inspector.
  for (auto& root : roots) {
    inspector.emplace(std::move(root));
  }
}

fidl::StringView CollectionName(Collection collection) {
  switch (collection) {
    case Collection::kNone:
      return {};
    case Collection::kHost:
      return "driver-hosts";
    case Collection::kBoot:
      return "boot-drivers";
    case Collection::kPackage:
      return "pkg-drivers";
  }
}

Node* PrimaryParent(const std::vector<Node*>& parents) {
  return parents.empty() ? nullptr : parents[0];
}

void CloseAndReset(std::optional<fidl::ServerBindingRef<frunner::ComponentController>>& ref) {
  if (ref) {
    // Send an epitaph to the component manager and close the connection. The
    // server of a `ComponentController` protocol is expected to send an epitaph
    // before closing the associated connection.
    ref->Close(ZX_OK);
    ref.reset();
  }
}

template <typename T>
void UnbindAndReset(std::optional<fidl::ServerBindingRef<T>>& ref) {
  if (ref) {
    ref->Unbind();
    ref.reset();
  }
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

DriverComponent::DriverComponent(fidl::ClientEnd<fdf::Driver> driver)
    : driver_(std::move(driver)), wait_(this, driver_.channel().get(), ZX_CHANNEL_PEER_CLOSED) {}

void DriverComponent::set_driver_ref(
    fidl::ServerBindingRef<frunner::ComponentController> driver_ref) {
  driver_ref_.emplace(std::move(driver_ref));
}

zx::status<> DriverComponent::Watch(async_dispatcher_t* dispatcher) {
  zx_status_t status = wait_.Begin(dispatcher);
  return zx::make_status(status);
}

void DriverComponent::Stop(StopRequestView request,
                           DriverComponent::StopCompleter::Sync& completer) {
  Stop();
}

void DriverComponent::Kill(KillRequestView request,
                           DriverComponent::KillCompleter::Sync& completer) {
  Stop();
}

void DriverComponent::Stop() {
  // Stop waiting on `driver_`, if there is a wait pending.
  if (wait_.is_pending()) {
    if (zx_status_t status = wait_.Cancel(); status != ZX_OK) {
      LOGF(WARNING, "Failed to cancel watch on driver: %s", zx_status_get_string(status));
    }
  }
  // Close `driver_` to signal to the driver host that the associated driver
  // component should be stopped.
  driver_.reset();
}

void DriverComponent::OnPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    LOGF(WARNING, "Failed to watch driver: %s", zx_status_get_string(status));
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    CloseAndReset(driver_ref_);
  }
}

DriverHostComponent::DriverHostComponent(
    fidl::ClientEnd<fdf::DriverHost> driver_host, async_dispatcher_t* dispatcher,
    fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
    : driver_host_(std::move(driver_host), dispatcher,
                   fidl::ObserveTeardown([this, driver_hosts] { driver_hosts->erase(*this); })) {}

zx::status<fidl::ClientEnd<fdf::Driver>> DriverHostComponent::Start(
    fidl::ClientEnd<fdf::Node> client_end, const Node& node,
    frunner::wire::ComponentStartInfo start_info) {
  auto endpoints = fidl::CreateEndpoints<fdf::Driver>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto binary = driver::ProgramValue(start_info.program(), "binary").value_or("");
  fidl::Arena arena;
  fdf::wire::DriverStartArgs args(arena);
  args.set_node(std::move(client_end))
      .set_url(arena, start_info.resolved_url())
      .set_program(arena, start_info.program())
      .set_ns(arena, start_info.ns())
      .set_outgoing_dir(std::move(start_info.outgoing_dir()));
  if (auto symbols = node.symbols(); !symbols.empty()) {
    args.set_symbols(arena, symbols);
  }
  auto start = driver_host_->Start(args, std::move(endpoints->server));
  if (!start.ok()) {
    LOGF(ERROR, "Failed to start driver '%s' in driver host: %s", binary.data(),
         start.FormatDescription().data());
    return zx::error(start.status());
  }
  return zx::ok(std::move(endpoints->client));
}

Node::Node(std::string_view name, std::vector<Node*> parents, DriverBinder* driver_binder,
           async_dispatcher_t* dispatcher)
    : name_(name),
      parents_(std::move(parents)),
      driver_binder_(driver_binder),
      dispatcher_(dispatcher) {
  if (auto primary_parent = PrimaryParent(parents_)) {
    // By default, we set `driver_host_` to match the primary parent's
    // `driver_host_`. If the node is then subsequently bound to a driver in a
    // different driver host, this value will be updated to match.
    driver_host_ = primary_parent->driver_host_;
  }
}

Node::~Node() { UnbindAndReset(controller_ref_); }

const std::string& Node::name() const { return name_; }

const std::vector<std::shared_ptr<Node>>& Node::children() const { return children_; }

std::vector<Node::OwnedOffer>& Node::offers() const {
  // TODO(fxbug.dev/66150): Once FIDL wire types support a Clone() method,
  // remove the const_cast.
  return const_cast<decltype(offers_)&>(offers_);
}

fidl::VectorView<fdf::wire::NodeSymbol> Node::symbols() const {
  auto primary_parent = PrimaryParent(parents_);
  if (primary_parent != nullptr && primary_parent->driver_host_ == driver_host_) {
    // If this node is colocated with its parent, then provide the symbols.
    // TODO(fxbug.dev/7999): Remove const_cast once VectorView supports const.
    return fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(
        const_cast<decltype(symbols_)&>(symbols_));
  }
  return {};
}

DriverHostComponent* Node::driver_host() const { return *driver_host_; }

void Node::set_collection(Collection collection) { collection_ = collection; }

void Node::set_driver_host(DriverHostComponent* driver_host) { driver_host_ = driver_host; }

void Node::set_driver_ref(
    std::optional<fidl::ServerBindingRef<frunner::ComponentController>> driver_ref) {
  driver_ref_ = std::move(driver_ref);
}

void Node::set_controller_ref(fidl::ServerBindingRef<fdf::NodeController> controller_ref) {
  controller_ref_.emplace(std::move(controller_ref));
}

void Node::set_node_ref(fidl::ServerBindingRef<fdf::Node> node_ref) {
  node_ref_.emplace(std::move(node_ref));
}

std::string Node::TopoName() const {
  std::deque<std::string_view> names;
  for (auto node = this; node != nullptr; node = PrimaryParent(node->parents_)) {
    names.push_front(node->name());
  }
  return fxl::JoinStrings(names, ".");
}

fidl::VectorView<fdecl::wire::Offer> Node::CreateOffers(fidl::AnyArena& arena) const {
  std::vector<fdecl::wire::Offer> node_offers;
  for (const Node* parent : parents_) {
    // Find a parent node with a collection. This indicates that a driver has
    // been bound to the node, and the driver is running within the collection.
    auto source_node = parent;
    for (; source_node->collection_ == Collection::kNone && source_node != nullptr;
         source_node = PrimaryParent(source_node->parents_)) {
    }
    // If this is a composite node, then the offers come from the parent nodes.
    auto& parent_offers = parents_.size() == 1 ? offers() : parent->offers();
    node_offers.reserve(node_offers.size() + parent_offers.size());
    for (auto& parent_offer : parent_offers) {
      fdecl::wire::Offer& offer = parent_offer->get();
      VisitOffer<bool>(offer, [&arena, source_node](auto& decl) mutable {
        // Assign the source of the offer.
        fdecl::wire::ChildRef source_ref{
            .name = {arena, source_node->TopoName()},
            .collection = CollectionName(source_node->collection_),
        };
        decl.set_source(arena, fdecl::wire::Ref::WithChild(arena, source_ref));
        return true;
      });
      node_offers.push_back(offer);
    }
  }
  fidl::VectorView<fdecl::wire::Offer> out(arena, node_offers.size());
  std::copy(node_offers.begin(), node_offers.end(), out.begin());
  return out;
}

void Node::OnBind() const {
  if (controller_ref_) {
    fidl::Result result = (*controller_ref_)->OnBind();
    if (!result.ok()) {
      LOGF(ERROR, "Failed to send OnBind event: %s", result.FormatDescription().data());
    }
  }
}

bool Node::Unbind(std::unique_ptr<AsyncRemove>& async_remove) {
  bool has_driver = driver_ref_.has_value();
  CloseAndReset(driver_ref_);
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
  auto this_node = shared_from_this();
  if (!parents_.empty()) {
    for (auto parent : parents_) {
      auto& children = parent->children_;
      children.erase(std::find(children.begin(), children.end(), this_node));
      // We are removing a composite node. This means its parent nodes only
      // exist to form a composite node, and they are not directly owned by a
      // driver. Therefore, we should remove the parent node as well.
      if (parents_.size() > 1) {
        parent->Remove();
      }
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

void Node::AddToParents() {
  auto this_node = shared_from_this();
  for (auto parent : parents_) {
    parent->children_.push_back(this_node);
  }
}

void Node::AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) {
  if (driver_binder_ == nullptr) {
    LOGF(ERROR, "Failed to add Node, as this Node '%s' was removed", name().data());
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
    LOGF(ERROR, "Failed to add Node '%.*s', name must not contain '.'",
         static_cast<int>(name.size()), name.data());
    completer.ReplyError(fdf::wire::NodeError::kNameInvalid);
    return;
  }
  for (auto& child : children_) {
    if (child->name() == name) {
      LOGF(ERROR, "Failed to add Node '%.*s', name already exists among siblings",
           static_cast<int>(name.size()), name.data());
      completer.ReplyError(fdf::wire::NodeError::kNameAlreadyExists);
      return;
    }
  };
  auto child = std::make_shared<Node>(name, std::vector<Node*>{this}, *driver_binder_, dispatcher_);

  if (request->args.has_offers()) {
    child->offers_.reserve(request->args.offers().count());
    for (auto& offer : request->args.offers()) {
      auto has_source_name =
          VisitOffer<bool>(offer, [](auto& decl) { return decl.has_source_name(); });
      if (!has_source_name.value_or(false)) {
        LOGF(ERROR, "Failed to add Node '%.*s', an offer must have a source name",
             static_cast<int>(name.size()), name.data());
        completer.ReplyError(fdf::wire::NodeError::kOfferSourceNameMissing);
        return;
      }
      auto has_ref = VisitOffer<bool>(
          offer, [](auto& decl) { return decl.has_source() || decl.has_target(); });
      if (has_ref.value_or(false)) {
        LOGF(ERROR, "Failed to add Node '%.*s', an offer must not have a source or target",
             static_cast<int>(name.size()), name.data());
        completer.ReplyError(fdf::wire::NodeError::kOfferRefExists);
        return;
      }

      child->offers_.push_back(OwnedMessage<fdecl::wire::Offer>::From(offer));
    }
  }

  if (request->args.has_symbols()) {
    child->symbols_.reserve(request->args.symbols().count());
    std::unordered_set<std::string_view> names;
    for (auto& symbol : request->args.symbols()) {
      if (!symbol.has_name()) {
        LOGF(ERROR, "Failed to add Node '%.*s', a symbol is missing a name",
             static_cast<int>(name.size()), name.data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolNameMissing);
        return;
      }
      if (!symbol.has_address()) {
        LOGF(ERROR, "Failed to add Node '%.*s', symbol '%.*s' is missing an address",
             static_cast<int>(name.size()), name.data(), static_cast<int>(symbol.name().size()),
             symbol.name().data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolAddressMissing);
        return;
      }
      auto inserted = names.emplace(symbol.name().data(), symbol.name().size()).second;
      if (!inserted) {
        LOGF(ERROR, "Failed to add Node '%.*s', symbol '%.*s' already exists",
             static_cast<int>(name.size()), name.data(), static_cast<int>(symbol.name().size()),
             symbol.name().data());
        completer.ReplyError(fdf::wire::NodeError::kSymbolAlreadyExists);
        return;
      }
      fdf::wire::NodeSymbol node_symbol(child->arena_);
      node_symbol.set_name(child->arena_, child->arena_, symbol.name().get());
      node_symbol.set_address(child->arena_, symbol.address());
      child->symbols_.push_back(std::move(node_symbol));
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
  } else {
    (*driver_binder_)->Bind(*child, std::move(request->args));
  }
  child->AddToParents();
  // We do not block a driver from operation after it has added a child. If the
  // child is waiting to be bound, it is owned by the driver runner.
  completer.ReplySuccess();
}

DriverRunner::DriverRunner(fidl::ClientEnd<fcomponent::Realm> realm,
                           fidl::ClientEnd<fdf::DriverIndex> driver_index,
                           inspect::Inspector& inspector, async_dispatcher_t* dispatcher)
    : realm_(std::move(realm), dispatcher),
      driver_index_(std::move(driver_index), dispatcher),
      dispatcher_(dispatcher),
      root_node_(std::make_shared<Node>("root", std::vector<Node*>{}, this, dispatcher)) {
  inspector.GetRoot().CreateLazyNode(
      "driver_runner", [this] { return Inspect(); }, &inspector);
}

fpromise::promise<inspect::Inspector> DriverRunner::Inspect() const {
  inspect::Inspector inspector;
  auto root = inspector.GetRoot().CreateChild(root_node_->name());
  InspectStack stack{{std::make_pair(&root, root_node_.get())}};
  InspectNode(inspector, stack);
  inspector.emplace(std::move(root));
  return fpromise::make_ok_promise(inspector);
}

size_t DriverRunner::NumOrphanedNodes() const { return orphaned_nodes_.size(); }

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
  return StartDriver(*root_node_, url);
}

zx::status<> DriverRunner::StartDriver(Node& node, std::string_view url) {
  zx::event token;
  zx_status_t status = zx::event::create(0, &token);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  zx_info_handle_basic_t info{};
  status = token.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto collection = cpp20::starts_with(url, kBootScheme) ? Collection::kBoot : Collection::kPackage;
  node.set_collection(collection);
  auto create = CreateComponent(node.TopoName(), collection, std::string(url),
                                {.node = &node, .token = std::move(token)});
  if (create.is_error()) {
    return create.take_error();
  }
  driver_args_.emplace(info.koid, node);
  return zx::ok();
}

void DriverRunner::Start(StartRequestView request, StartCompleter::Sync& completer) {
  auto url = request->start_info.resolved_url().get();

  // When we start a driver, we associate an unforgeable token (the KOID of a
  // zx::event) with the start request, through the use of the numbered_handles
  // field. We do this so:
  //  1. We can securely validate the origin of the request
  //  2. We avoid collisions that can occur when relying on the package URL
  //  3. We avoid relying on the resolved URL matching the package URL
  if (!request->start_info.has_numbered_handles()) {
    LOGF(ERROR, "Failed to start driver '%.*s', invalid request for driver",
         static_cast<int>(url.size()), url.data());
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  auto& handles = request->start_info.numbered_handles();
  if (handles.count() != 1 || !handles[0].handle || handles[0].id != kTokenId) {
    LOGF(ERROR, "Failed to start driver '%.*s', invalid request for driver",
         static_cast<int>(url.size()), url.data());
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  zx_info_handle_basic_t info{};
  zx_status_t status =
      handles[0].handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  auto it = driver_args_.find(info.koid);
  if (it == driver_args_.end()) {
    LOGF(ERROR, "Failed to start driver '%.*s', unknown request for driver",
         static_cast<int>(url.size()), url.data());
    completer.Close(ZX_ERR_UNAVAILABLE);
    return;
  }
  auto& [_, node] = *it;
  driver_args_.erase(it);

  // Launch a driver host, or use an existing driver host.
  if (driver::ProgramValue(request->start_info.program(), "colocate").value_or("") == "true") {
    if (&node == root_node_.get()) {
      LOGF(ERROR, "Failed to start driver '%.*s', root driver cannot colocate",
           static_cast<int>(url.size()), url.data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  } else {
    auto result = StartDriverHost();
    if (result.is_error()) {
      completer.Close(result.error_value());
      return;
    }
    node.set_driver_host(result.value().get());
    driver_hosts_.push_back(std::move(*result));
  }

  // Bind the Node associated with the driver.
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();
  if (endpoints.is_error()) {
    completer.Close(endpoints.error_value());
    return;
  }
  auto bind_node = fidl::BindServer<fidl::WireServer<fdf::Node>>(
      dispatcher_, std::move(endpoints->server), &node,
      [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
  node.set_node_ref(bind_node);

  // Start the driver within the driver host.
  auto start =
      node.driver_host()->Start(std::move(endpoints->client), node, std::move(request->start_info));
  if (start.is_error()) {
    completer.Close(start.error_value());
    return;
  }

  // Create a DriverComponent to manage the driver.
  auto driver = std::make_unique<DriverComponent>(std::move(*start));
  auto bind_driver =
      fidl::BindServer(dispatcher_, std::move(request->controller), driver.get(),
                       [this, node = node.weak_from_this()](DriverComponent* driver, auto, auto) {
                         // When the driver is unbound, we need to inform the
                         // associated node that its driver ref is no longer
                         // valid, and that there is no longer a driver bound to
                         // the node.
                         if (auto ptr = node.lock()) {
                           ptr->set_driver_ref({});
                         }
                         drivers_.erase(*driver);
                       });
  node.set_driver_ref(bind_driver);
  driver->set_driver_ref(std::move(bind_driver));
  auto watch = driver->Watch(dispatcher_);
  if (watch.is_error()) {
    LOGF(ERROR, "Failed to watch channel for driver '%.*s': %s", static_cast<int>(url.size()),
         url.data(), watch.status_string());
    completer.Close(watch.error_value());
    return;
  }
  drivers_.push_back(std::move(driver));
}

void DriverRunner::Bind(Node& node, fdf::wire::NodeAddArgs args) {
  auto match_callback = [this,
                         &node](fidl::WireUnownedResult<fdf::DriverIndex::MatchDriver>& result) {
    auto driver_node = &node;
    auto orphaned = [this, &driver_node] {
      orphaned_nodes_.push_back(driver_node->weak_from_this());
    };

    if (!result.ok()) {
      orphaned();
      LOGF(ERROR, "Failed to call match Node '%s': %s", node.name().data(),
           result.error().FormatDescription().data());
      return;
    }

    if (result->result.is_err()) {
      orphaned();
      LOGF(ERROR, "Failed to match Node '%s': %s", driver_node->name().data(),
           zx_status_get_string(result->result.err()));
      return;
    }
    auto& matched_driver = result->result.response().driver;
    if (!matched_driver.has_url()) {
      orphaned();
      LOGF(ERROR, "Failed to match Node '%s', the driver URL is missing",
           driver_node->name().data());
      return;
    }

    // This is a composite driver, create a composite node for it.
    if (matched_driver.has_node_index() || matched_driver.has_num_nodes()) {
      auto composite = CreateCompositeNode(node, matched_driver);
      if (composite.is_error()) {
        // CreateCompositeNode() handles orphaned nodes.
        return;
      }
      driver_node = *composite;
    }

    auto start_result = StartDriver(*driver_node, matched_driver.url().get());
    if (start_result.is_error()) {
      orphaned();
      LOGF(ERROR, "Failed to start driver '%s': %s", driver_node->name().data(),
           zx_status_get_string(start_result.error_value()));
    }
    node.OnBind();
  };
  driver_index_->MatchDriver(args, std::move(match_callback));
}

zx::status<Node*> DriverRunner::CreateCompositeNode(
    Node& node, const fdf::wire::MatchedDriver& matched_driver) {
  auto it = AddToCompositeArgs(node.name(), matched_driver);
  if (it.is_error()) {
    orphaned_nodes_.push_back(node.weak_from_this());
    return it.take_error();
  }
  auto& [_, nodes] = **it;

  std::vector<Node*> parents;
  // Store the node arguments inside the composite arguments.
  nodes[matched_driver.node_index()] = node.weak_from_this();
  // Check if we have all the nodes for the composite driver.
  for (auto& node : nodes) {
    if (auto parent = node.lock()) {
      parents.push_back(parent.get());
    } else {
      // We are missing a node or it has been removed, continue to wait.
      return zx::error(ZX_ERR_NEXT);
    }
  }
  composite_args_.erase(*it);

  // We have all the nodes, create a composite node for the composite driver.
  auto composite = std::make_shared<Node>("composite", std::move(parents), this, dispatcher_);
  composite->AddToParents();
  // We can return a pointer, as the composite node is owned by its parents.
  return zx::ok(composite.get());
}

zx::status<DriverRunner::CompositeArgsIterator> DriverRunner::AddToCompositeArgs(
    const std::string& name, const fdf::wire::MatchedDriver& matched_driver) {
  if (!matched_driver.has_node_index() || !matched_driver.has_num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', missing fields for composite driver", name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (matched_driver.node_index() >= matched_driver.num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', the node index is out of range", name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Check if there are existing composite arguments for the composite driver.
  // We do this by checking if the node index within an existing set of
  // composite arguments has not been set, or has become available.
  auto [it, end] = composite_args_.equal_range(std::string(matched_driver.url().get()));
  for (; it != end; ++it) {
    auto& [_, nodes] = *it;
    if (nodes.size() != matched_driver.num_nodes()) {
      LOGF(ERROR, "Failed to match Node '%s', the number of nodes does not match", name.data());
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (nodes[matched_driver.node_index()].expired()) {
      break;
    }
  }
  // No composite arguments exist for the composite driver, create a new set.
  if (it == end) {
    it = composite_args_.emplace(matched_driver.url().get(),
                                 CompositeArgs{matched_driver.num_nodes()});
  }
  return zx::ok(it);
}

zx::status<std::unique_ptr<DriverHostComponent>> DriverRunner::StartDriverHost() {
  zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto name = "driver-host-" + std::to_string(next_driver_host_id_++);
  auto create = CreateComponent(name, Collection::kHost, "#meta/driver_host2.cm",
                                {.exposed_dir = std::move(endpoints->server)});
  if (create.is_error()) {
    return create.take_error();
  }

  auto client_end = service::ConnectAt<fdf::DriverHost>(endpoints->client);
  if (client_end.is_error()) {
    LOGF(ERROR, "Failed to connect to service '%s': %s",
         fidl::DiscoverableProtocolName<fdf::DriverHost>, client_end.status_string());
    return client_end.take_error();
  }

  auto driver_host =
      std::make_unique<DriverHostComponent>(std::move(*client_end), dispatcher_, &driver_hosts_);
  return zx::ok(std::move(driver_host));
}

zx::status<> DriverRunner::CreateComponent(std::string name, Collection collection, std::string url,
                                           CreateComponentOpts opts) {
  fidl::Arena arena;
  fdecl::wire::Child child_decl(arena);
  child_decl.set_name(arena, fidl::StringView::FromExternal(name))
      .set_url(arena, fidl::StringView::FromExternal(url))
      .set_startup(fdecl::wire::StartupMode::kLazy);
  fcomponent::wire::CreateChildArgs child_args(arena);
  if (opts.node != nullptr) {
    child_args.set_dynamic_offers(arena, opts.node->CreateOffers(arena));
  }
  fprocess::wire::HandleInfo handle_info;
  if (opts.token) {
    handle_info = {
        .handle = std::move(opts.token),
        .id = kTokenId,
    };
    child_args.set_numbered_handles(
        arena, fidl::VectorView<fprocess::wire::HandleInfo>::FromExternal(&handle_info, 1));
  }
  auto open_callback = [name,
                        url](fidl::WireUnownedResult<fcomponent::Realm::OpenExposedDir>& result) {
    if (!result.ok()) {
      LOGF(ERROR, "Failed to open exposed directory for component '%s' (%s): %s", name.data(),
           url.data(), result.FormatDescription().data());
      return;
    }
    if (result->result.is_err()) {
      LOGF(ERROR, "Failed to open exposed directory for component '%s' (%s): %u", name.data(),
           url.data(), result->result.err());
    }
  };
  auto create_callback =
      [this, name, url, collection, exposed_dir = std::move(opts.exposed_dir),
       open_callback = std::move(open_callback)](
          fidl::WireUnownedResult<fcomponent::Realm::CreateChild>& result) mutable {
        if (!result.ok()) {
          LOGF(ERROR, "Failed to create component '%s' (%s): %s", name.data(), url.data(),
               result.error().FormatDescription().data());
          return;
        }
        if (result->result.is_err()) {
          LOGF(ERROR, "Failed to create component '%s' (%s): %u", name.data(), url.data(),
               result->result.err());
          return;
        }
        if (exposed_dir) {
          fdecl::wire::ChildRef child_ref{
              .name = fidl::StringView::FromExternal(name),
              .collection = CollectionName(collection),
          };
          realm_->OpenExposedDir(child_ref, std::move(exposed_dir), std::move(open_callback));
        }
      };
  realm_->CreateChild(fdecl::wire::CollectionRef{.name = CollectionName(collection)}, child_decl,
                      child_args, std::move(create_callback));
  return zx::ok();
}
