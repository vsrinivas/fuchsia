// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_runner.h"

#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.driver.host/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.process/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/driver2/start_args.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/service.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <deque>
#include <forward_list>
#include <queue>
#include <stack>
#include <unordered_set>

#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdf = fuchsia_driver_framework;
namespace fdh = fuchsia_driver_host;
namespace fdi = fuchsia_driver_index;
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
  switch (offer.Which()) {
    case fdecl::wire::Offer::Tag::kService:
      return apply(offer.service());
    case fdecl::wire::Offer::Tag::kProtocol:
      return apply(offer.protocol());
    case fdecl::wire::Offer::Tag::kDirectory:
      return apply(offer.directory());
    case fdecl::wire::Offer::Tag::kStorage:
      return apply(offer.storage());
    case fdecl::wire::Offer::Tag::kRunner:
      return apply(offer.runner());
    case fdecl::wire::Offer::Tag::kResolver:
      return apply(offer.resolver());
    case fdecl::wire::Offer::Tag::kEvent:
      return apply(offer.event());
    case fdecl::wire::Offer::Tag::kEventStream:
      return apply(offer.event_stream());
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
    std::string driver_string = "unbound";
    if (node->driver_component()) {
      driver_string = std::string((*node->driver_component())->url());
    }
    root->CreateString("driver", driver_string, &inspector);

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
    case Collection::kUniversePackage:
      return "universe-pkg-drivers";
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

std::optional<fdecl::wire::Offer> CreateCompositeDirOffer(fidl::AnyArena& arena,
                                                          fdecl::wire::Offer& offer,
                                                          std::string_view parents_name) {
  if (!offer.is_directory() || !offer.directory().has_target_name()) {
    return std::nullopt;
  }

  std::string target_name =
      std::string(offer.directory().target_name().data(), offer.directory().target_name().size());
  size_t split_index = target_name.rfind('-');
  if (split_index == std::string::npos) {
    return std::nullopt;
  }
  std::string dir_name = target_name.substr(0, split_index);
  std::string instance_name = target_name.substr(split_index + 1, target_name.size());

  // We only update directories that route the 'default' instance.
  if (instance_name != "default") {
    return std::nullopt;
  }
  // We have to create a new offer so we aren't manipulating our parent's offer.
  auto dir = fdecl::wire::OfferDirectory::Builder(arena);
  if (offer.directory().has_source_name()) {
    dir.source_name(offer.directory().source_name());
  }
  dir.target_name(arena, dir_name + std::string("-").append(parents_name));
  if (offer.directory().has_rights()) {
    dir.rights(offer.directory().rights());
  }
  if (offer.directory().has_subdir()) {
    dir.subdir(offer.directory().subdir());
  }
  if (offer.directory().has_dependency_type()) {
    dir.dependency_type(offer.directory().dependency_type());
  }
  if (offer.directory().has_source()) {
    dir.source(offer.directory().source());
  }
  if (offer.directory().has_target()) {
    dir.target(offer.directory().target());
  }
  return fdecl::wire::Offer::WithDirectory(arena, dir.Build());
}

DriverComponent::DriverComponent(fidl::ClientEnd<fdh::Driver> driver,
                                 async_dispatcher_t* dispatcher, std::string_view url)
    : driver_(std::move(driver), dispatcher, this), url_(url) {}

DriverComponent::~DriverComponent() {
  node_->set_driver_component(std::nullopt);
  node_->Remove();
}

std::string_view DriverComponent::url() const { return url_; }

void DriverComponent::set_driver_ref(
    fidl::ServerBindingRef<frunner::ComponentController> driver_ref) {
  driver_ref_.emplace(std::move(driver_ref));
}

void DriverComponent::on_fidl_error(fidl::UnbindInfo info) {
  // The only valid way a driver host should shut down the Driver channel
  // is with the ZX_OK epitaph.
  if (info.reason() != fidl::Reason::kPeerClosed || info.status() != ZX_OK) {
    LOGF(ERROR, "DriverComponent: %s: driver channel shutdown with: %s", url_.data(),
         info.FormatDescription().data());
  }

  // We are disconnected from the DriverHost so shut everything down.
  StopComponent();
}

void DriverComponent::Stop(StopRequestView request,
                           DriverComponent::StopCompleter::Sync& completer) {
  RequestDriverStop();
}

void DriverComponent::Kill(KillRequestView request,
                           DriverComponent::KillCompleter::Sync& completer) {
  RequestDriverStop();
}

void DriverComponent::StopComponent() { CloseAndReset(driver_ref_); }

void DriverComponent::RequestDriverStop() { node_->Remove(); }

void DriverComponent::StopDriver() {
  if (stop_in_progress_) {
    return;
  }

  auto result = driver_->Stop();
  if (!result.ok()) {
    LOGF(ERROR, "Failed to stop a driver: %s", result.FormatDescription().data());
  }
  stop_in_progress_ = true;
}

DriverHostComponent::DriverHostComponent(
    fidl::ClientEnd<fdh::DriverHost> driver_host, async_dispatcher_t* dispatcher,
    fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
    : driver_host_(std::move(driver_host), dispatcher,
                   fidl::ObserveTeardown([this, driver_hosts] { driver_hosts->erase(*this); })) {}

zx::status<fidl::ClientEnd<fdh::Driver>> DriverHostComponent::Start(
    fidl::ClientEnd<fdf::Node> client_end, const Node& node,
    frunner::wire::ComponentStartInfo start_info) {
  auto endpoints = fidl::CreateEndpoints<fdh::Driver>();
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
  if (start_info.has_encoded_config()) {
    if (start_info.encoded_config().is_buffer()) {
      args.set_config(std::move(start_info.encoded_config().buffer().vmo));
    } else if (start_info.encoded_config().is_bytes()) {
      auto vmo_size = start_info.encoded_config().bytes().count();
      zx::vmo vmo;

      auto status = zx::vmo::create(vmo_size, ZX_RIGHT_TRANSFER | ZX_RIGHT_READ, &vmo);
      if (status != ZX_OK) {
        return zx::error(status);
      }

      status = vmo.write(start_info.encoded_config().bytes().data(), 0, vmo_size);
      if (status != ZX_OK) {
        return zx::error(status);
      }

      args.set_config(std::move(vmo));
    } else {
      LOGF(ERROR, "Failed to parse encoded config in start info. Encoding is not buffer or bytes.");
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }
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

zx::status<uint64_t> DriverHostComponent::GetProcessKoid() {
  auto result = driver_host_.sync()->GetProcessKoid();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result.Unwrap_NEW()->is_error()) {
    return zx::error(result.Unwrap_NEW()->error_value());
  }
  return zx::ok(result.Unwrap_NEW()->value()->koid);
}

BindResultTracker::BindResultTracker(size_t expected_result_count,
                                     NodeBindingInfoResultCallback result_callback)
    : expected_result_count_(expected_result_count),
      currently_reported_(0),
      result_callback_(std::move(result_callback)) {}

void BindResultTracker::ReportNoBind() {
  size_t current;
  {
    std::scoped_lock guard(lock_);
    currently_reported_++;
    current = currently_reported_;
  }

  Complete(current);
}

void BindResultTracker::ReportSuccessfulBind(const std::string_view& node_name,
                                             const std::string_view& driver) {
  size_t current;
  {
    std::scoped_lock guard(lock_);
    currently_reported_++;
    auto node_binding_info = fuchsia_driver_development::wire::NodeBindingInfo::Builder(arena_)
                                 .node_name(node_name)
                                 .driver_url(driver)
                                 .Build();
    results_.emplace_back(node_binding_info);
    current = currently_reported_;
  }

  Complete(current);
}

void BindResultTracker::Complete(size_t current) {
  if (current == expected_result_count_) {
    result_callback_(
        fidl::VectorView<fuchsia_driver_development::wire::NodeBindingInfo>(arena_, results_));
  }
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

const std::optional<DriverComponent*>& Node::driver_component() const { return driver_component_; }

const std::vector<Node*>& Node::parents() const { return parents_; }

const std::list<std::shared_ptr<Node>>& Node::children() const { return children_; }

std::vector<Node::OwnedOffer>& Node::offers() const {
  // TODO(fxbug.dev/66150): Once FIDL wire types support a Clone() method,
  // remove the const_cast.
  return const_cast<decltype(offers_)&>(offers_);
}

fidl::VectorView<fdf::wire::NodeSymbol> Node::symbols() const {
  auto primary_parent = PrimaryParent(parents_);
  // If this node is colocated with its parent, then provide the symbols.
  if (primary_parent != nullptr && primary_parent->driver_host_ == driver_host_) {
    // If we are a composite node, then take the symbols of our primary parent.
    auto& symbols = (parents_.size() > 1) ? primary_parent->symbols_ : symbols_;
    // TODO(fxbug.dev/7999): Remove const_cast once VectorView supports const.
    return fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(
        const_cast<decltype(symbols_)&>(symbols));
  }
  return {};
}

const std::vector<fdf::wire::NodeProperty>& Node::properties() const { return properties_; }

DriverHostComponent* Node::driver_host() const { return *driver_host_; }

void Node::set_collection(Collection collection) { collection_ = collection; }

void Node::set_driver_host(DriverHostComponent* driver_host) { driver_host_ = driver_host; }

void Node::set_controller_ref(fidl::ServerBindingRef<fdf::NodeController> controller_ref) {
  controller_ref_.emplace(std::move(controller_ref));
}

void Node::set_driver_component(std::optional<DriverComponent*> driver_component) {
  driver_component_ = driver_component;
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
  size_t parent_index = 0;
  bool is_composite = parents_.size() > 1;
  for (const Node* parent : parents_) {
    // Find a parent node with a collection. This indicates that a driver has
    // been bound to the node, and the driver is running within the collection.
    auto source_node = parent;
    for (; source_node != nullptr && source_node->collection_ == Collection::kNone;
         source_node = PrimaryParent(source_node->parents_)) {
    }
    // If this is a composite node, then the offers come from the parent nodes.
    auto& parent_offers = is_composite ? parent->offers() : offers();
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

      // If we are a composite node, then we route 'service' directories based on the parent's name.
      if (is_composite && offer.is_directory()) {
        auto new_offer = CreateCompositeDirOffer(arena, offer, parents_names_[parent_index]);
        if (new_offer) {
          node_offers.push_back(*new_offer);

          // If we aren't the primary parent, then skip adding the "default" directory.
          if (parent_index != 0) {
            continue;
          }
        }
      }
      node_offers.push_back(offer);
    }
    parent_index++;
  }
  fidl::VectorView<fdecl::wire::Offer> out(arena, node_offers.size());
  std::copy(node_offers.begin(), node_offers.end(), out.begin());
  return out;
}

fuchsia_driver_framework::wire::NodeAddArgs Node::CreateAddArgs(fidl::AnyArena& arena) {
  fuchsia_driver_framework::wire::NodeAddArgs args(arena);
  args.set_name(arena, arena, name());
  args.set_offers(arena, CreateOffers(arena));
  args.set_properties(
      arena,
      fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty>::FromExternal(properties_));
  args.set_symbols(
      arena, fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol>::FromExternal(symbols_));
  return args;
}

void Node::OnBind() const {
  if (controller_ref_) {
    fidl::Status result = fidl::WireSendEvent(*controller_ref_)->OnBind();
    if (!result.ok()) {
      LOGF(ERROR, "Failed to send OnBind event: %s", result.FormatDescription().data());
    }
  }
}

void Node::AddToParents() {
  auto this_node = shared_from_this();
  for (auto parent : parents_) {
    parent->children_.push_back(this_node);
  }
}

void Node::Remove() {
  removal_in_progress_ = true;

  // Disable driver binding for the node. This also prevents child nodes from
  // being added to this node.
  driver_binder_ = nullptr;

  // Ask each of our children to remove themselves.
  for (auto it = children_.begin(); it != children_.end();) {
    // We have to be careful here - Remove() could invalidate the iterator, so we increment the
    // iterator before we call Remove().
    auto child = it->get();
    ++it;
    child->Remove();
  }

  // If we have any children, return. It's too early to remove ourselves.
  // (The children will call back into this Remove function as they exit).
  if (!children_.empty()) {
    return;
  }

  // If we still have a driver bound to us, we tell it to stop.
  // (The Driver will call back into this Remove function once it stops).
  if (driver_component_.has_value()) {
    (*driver_component_)->StopDriver();
    return;
  }

  // Let the removal begin

  // Erase ourselves from each parent.
  auto this_node = shared_from_this();
  for (auto parent : parents_) {
    auto& children = parent->children_;
    children.erase(std::find(children.begin(), children.end(), this_node));

    // If our parent is waiting to be removed and we are its last child,
    // then remove it.
    // Also remove the parent if we are a composite node.
    if ((parent->removal_in_progress_ && children.empty()) || (parents_.size() > 1)) {
      parent->Remove();
    }
  }
  // It's no longer safe to access our parents, as they can free themselves now.
  parents_.clear();

  // Remove our controller and node servers. These hold the last shared_ptr
  // references to this node.
  UnbindAndReset(controller_ref_);
  UnbindAndReset(node_ref_);
}

void Node::Remove(RemoveRequestView request, RemoveCompleter::Sync& completer) { Remove(); }

void Node::AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) {
  if (driver_binder_ == nullptr) {
    LOGF(WARNING, "Failed to add Node, as this Node '%s' was removed", name().data());
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

  if (request->args.has_properties()) {
    child->properties_.reserve(request->args.properties().count() + 1);  // + 1 for DFv2 prop.
    for (auto& property : request->args.properties()) {
      auto node_property = fdf::wire::NodeProperty::Builder(child->arena_);
      if (property.has_key()) {
        using fdf::wire::NodePropertyKey;
        switch (property.key().Which()) {
          case NodePropertyKey::Tag::kIntValue:
            node_property.key(NodePropertyKey::WithIntValue(property.key().int_value()));
            break;
          case NodePropertyKey::Tag::kStringValue: {
            node_property.key(NodePropertyKey::WithStringValue(
                child->arena_,
                fidl::StringView(child->arena_, property.key().string_value().get())));
            break;
          }
          default:
            LOGF(ERROR, "NodeProperty has unknown key tag '0x%lx'", property.key().Which());
            completer.ReplyError(fdf::wire::NodeError::kInternal);
            return;
        }
      }
      if (property.has_value()) {
        using fdf::wire::NodePropertyValue;
        switch (property.value().Which()) {
          case NodePropertyValue::Tag::kIntValue:
          case NodePropertyValue::Tag::kBoolValue:
            node_property.value(property.value());
            break;
          case NodePropertyValue::Tag::kStringValue: {
            node_property.value(NodePropertyValue::WithStringValue(
                child->arena_,
                fidl::StringView(child->arena_, property.value().string_value().get())));
            break;
          }
          case NodePropertyValue::Tag::kEnumValue: {
            node_property.value(NodePropertyValue::WithEnumValue(
                child->arena_,
                fidl::StringView(child->arena_, property.value().enum_value().get())));

            break;
          }
          default:
            LOGF(ERROR, "NodeProperty has unknown value tag '0x%lx'", property.value().Which());
            completer.ReplyError(fdf::wire::NodeError::kInternal);
            return;
        }
      }
      child->properties_.emplace_back(node_property.Build());
    }
  }

  // We set a property for DFv2 devices.
  child->properties_.emplace_back(fdf::wire::NodeProperty::Builder(child->arena_)
                                      .key(fdf::wire::NodePropertyKey::WithStringValue(
                                          child->arena_, "fuchsia.driver.framework.dfv2"))
                                      .value(fdf::wire::NodePropertyValue::WithBoolValue(true))
                                      .Build());

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
        dispatcher_, std::move(request->node), child,
        [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
    child->set_node_ref(std::move(bind_node));
  } else {
    // We don't care about tracking binds here, sending nullptr is fine.
    (*driver_binder_)->Bind(*child, nullptr);
  }
  child->AddToParents();
  // We do not block a driver from operation after it has added a child. If the
  // child is waiting to be bound, it is owned by the driver runner.
  completer.ReplySuccess();
}

DriverRunner::DriverRunner(fidl::ClientEnd<fcomponent::Realm> realm,
                           fidl::ClientEnd<fdi::DriverIndex> driver_index,
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

  // Make the device tree inspect nodes.
  auto device_tree = inspector.GetRoot().CreateChild("node_topology");
  auto root = device_tree.CreateChild(root_node_->name());
  InspectStack stack{{std::make_pair(&root, root_node_.get())}};
  InspectNode(inspector, stack);
  inspector.emplace(std::move(root));
  inspector.emplace(std::move(device_tree));

  // Make the unbound composite devices inspect nodes.
  auto composite = inspector.GetRoot().CreateChild("unbound_composites");
  for (auto& args : composite_args_) {
    auto child = composite.CreateChild(args.first);
    for (size_t i = 0; i < args.second.size(); i++) {
      auto& node = args.second[i];
      if (auto real = node.lock()) {
        child.CreateString(std::string("parent-").append(std::to_string(i)), real->TopoName(),
                           &inspector);
      } else {
        child.CreateString(std::string("parent-").append(std::to_string(i)), "<empty>", &inspector);
      }
    }
    inspector.emplace(std::move(child));
  }
  inspector.emplace(std::move(composite));

  // Make the orphaned devices inspect nodes.
  auto orphans = inspector.GetRoot().CreateChild("orphan_nodes");
  for (size_t i = 0; i < orphaned_nodes_.size(); i++) {
    if (auto node = orphaned_nodes_[i].lock()) {
      orphans.CreateString(std::to_string(i), node->TopoName(), &inspector);
    }
  }
  inspector.emplace(std::move(orphans));

  return fpromise::make_ok_promise(inspector);
}

size_t DriverRunner::NumOrphanedNodes() const { return orphaned_nodes_.size(); }

zx::status<> DriverRunner::PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<frunner::ComponentRunner> request) {
    fidl::BindServer<fidl::WireServer<frunner::ComponentRunner>>(dispatcher_, std::move(request),
                                                                 this);
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
  return StartDriver(*root_node_, url, fdi::DriverPackageType::kBase);
}

std::shared_ptr<const Node> DriverRunner::root_node() const { return root_node_; }

void DriverRunner::ScheduleBaseDriversBinding() {
  driver_index_->WaitForBaseDrivers().Then(
      [this](fidl::WireUnownedResult<fdi::DriverIndex::WaitForBaseDrivers>& result) mutable {
        if (!result.ok()) {
          // It's possible in tests that the test can finish before WaitForBaseDrivers
          // finishes.
          if (result.status() == ZX_ERR_PEER_CLOSED) {
            LOGF(WARNING, "Connection to DriverIndex closed during WaitForBaseDrivers.");
          } else {
            LOGF(ERROR, "DriverIndex::WaitForBaseDrivers failed with: %s",
                 result.error().FormatDescription().c_str());
          }
          return;
        }

        TryBindAllOrphansUntracked();
      });
}

void DriverRunner::TryBindAllOrphans(NodeBindingInfoResultCallback result_callback) {
  // Clear our stored vector of orphaned nodes, we will repopulate it with the
  // new orphans.
  std::vector<std::weak_ptr<Node>> orphaned_nodes = std::move(orphaned_nodes_);
  orphaned_nodes_ = {};

  std::shared_ptr<BindResultTracker> tracker =
      std::make_shared<BindResultTracker>(orphaned_nodes.size(), std::move(result_callback));

  for (auto& weak_node : orphaned_nodes) {
    auto node = weak_node.lock();
    if (!node) {
      tracker->ReportNoBind();
      continue;
    }

    Bind(*node, tracker);
  }
}

void DriverRunner::TryBindAllOrphansUntracked() {
  NodeBindingInfoResultCallback empty_callback =
      [](fidl::VectorView<fuchsia_driver_development::wire::NodeBindingInfo>) {};
  TryBindAllOrphans(std::move(empty_callback));
}

zx::status<> DriverRunner::StartDriver(Node& node, std::string_view url,
                                       fdi::DriverPackageType package_type) {
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

  // TODO(fxb/98474) Stop doing the url prefix check and just rely on the package_type.
  auto collection = cpp20::starts_with(url, kBootScheme) ? Collection::kBoot : Collection::kPackage;
  if (package_type == fdi::DriverPackageType::kUniverse) {
    collection = Collection::kUniversePackage;
  }
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
      dispatcher_, std::move(endpoints->server), node.shared_from_this(),
      [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
  node.set_node_ref(bind_node);

  LOGF(INFO, "Binding %.*s to  %s", static_cast<int>(url.size()), url.data(), node.name().c_str());
  // Start the driver within the driver host.
  auto start =
      node.driver_host()->Start(std::move(endpoints->client), node, std::move(request->start_info));
  if (start.is_error()) {
    completer.Close(start.error_value());
    return;
  }

  // Create a DriverComponent to manage the driver.
  auto driver = std::make_unique<DriverComponent>(std::move(*start), dispatcher_, url);
  auto bind_driver =
      fidl::BindServer(dispatcher_, std::move(request->controller), driver.get(),
                       [this](DriverComponent* driver, auto, auto) { drivers_.erase(*driver); });
  node.set_driver_component(driver.get());
  driver->set_driver_ref(std::move(bind_driver));
  driver->set_node(node.shared_from_this());
  drivers_.push_back(std::move(driver));
}

void DriverRunner::Bind(Node& node, std::shared_ptr<BindResultTracker> result_tracker) {
  auto match_callback = [this, weak_node = node.weak_from_this(), result_tracker](
                            fidl::WireUnownedResult<fdi::DriverIndex::MatchDriver>& result) {
    auto shared_node = weak_node.lock();

    auto report_no_bind = fit::defer([&result_tracker]() {
      if (result_tracker) {
        result_tracker->ReportNoBind();
      }
    });

    if (!shared_node) {
      LOGF(WARNING, "Node was freed before it could be bound");
      return;
    }

    Node& node = *shared_node;
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

    if (result.Unwrap_NEW()->is_error()) {
      orphaned();
      // Log the failed MatchDriver only if we are not tracking the results with a tracker
      // or if the error is not a ZX_ERR_NOT_FOUND error (meaning it could not find a driver).
      // When we have a tracker, the bind is happening for all the orphan nodes and the
      // not found errors get very noisy.
      zx_status_t match_error = result.Unwrap_NEW()->error_value();
      if (!result_tracker || match_error != ZX_ERR_NOT_FOUND) {
        LOGF(WARNING, "Failed to match Node '%s': %s", driver_node->name().data(),
             zx_status_get_string(match_error));
      }

      return;
    }

    auto& matched_driver = result.Unwrap_NEW()->value()->driver;
    if (!matched_driver.is_driver() && !matched_driver.is_composite_driver()) {
      orphaned();
      LOGF(WARNING,
           "Failed to match Node '%s', the MatchedDriver is not a normal or composite"
           "driver.",
           driver_node->name().data());
      return;
    }

    if (matched_driver.is_composite_driver() &&
        !matched_driver.composite_driver().has_driver_info()) {
      orphaned();
      LOGF(WARNING,
           "Failed to match Node '%s', the MatchedDriver is missing driver info for a composite "
           "driver.",
           driver_node->name().data());
      return;
    }

    auto driver_info = matched_driver.is_driver() ? matched_driver.driver()
                                                  : matched_driver.composite_driver().driver_info();

    if (!driver_info.has_url()) {
      orphaned();
      LOGF(ERROR, "Failed to match Node '%s', the driver URL is missing",
           driver_node->name().data());
      return;
    }

    // This is a composite driver, create a composite node for it.
    if (matched_driver.is_composite_driver()) {
      auto composite = CreateCompositeNode(node, matched_driver.composite_driver());

      // Orphaned nodes are handled by CreateCompositeNode().
      if (composite.is_error()) {
        return;
      }
      driver_node = *composite;
    }

    auto pkg_type =
        driver_info.has_package_type() ? driver_info.package_type() : fdi::DriverPackageType::kBase;
    auto start_result = StartDriver(*driver_node, driver_info.url().get(), pkg_type);
    if (start_result.is_error()) {
      orphaned();
      LOGF(ERROR, "Failed to start driver '%s': %s", driver_node->name().data(),
           zx_status_get_string(start_result.error_value()));
      return;
    }

    node.OnBind();
    report_no_bind.cancel();
    if (result_tracker) {
      result_tracker->ReportSuccessfulBind(node.TopoName(), driver_info.url().get());
    }
  };
  fidl::Arena<> arena;
  driver_index_->MatchDriver(node.CreateAddArgs(arena)).Then(std::move(match_callback));
}

zx::status<Node*> DriverRunner::CreateCompositeNode(
    Node& node, const fdi::wire::MatchedCompositeInfo& matched_driver) {
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
  std::vector<std::string> parents_names;
  for (auto name : matched_driver.node_names()) {
    parents_names.emplace_back(name.data(), name.size());
  }

  composite->set_parents_names(std::move(parents_names));
  composite->AddToParents();
  // We can return a pointer, as the composite node is owned by its parents.
  return zx::ok(composite.get());
}

zx::status<DriverRunner::CompositeArgsIterator> DriverRunner::AddToCompositeArgs(
    const std::string& name, const fdi::wire::MatchedCompositeInfo& composite_info) {
  if (!composite_info.has_node_index() || !composite_info.has_num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', missing fields for composite driver", name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (composite_info.node_index() >= composite_info.num_nodes()) {
    LOGF(ERROR, "Failed to match Node '%s', the node index is out of range", name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (!composite_info.has_driver_info() || !composite_info.driver_info().has_url()) {
    LOGF(ERROR, "Failed to match Node '%s', missing driver info fields for composite driver",
         name.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  auto url = std::string(composite_info.driver_info().url().get());

  // Check if there are existing composite arguments for the composite driver.
  // We do this by checking if the node index within an existing set of
  // composite arguments has not been set, or has become available.
  auto [it, end] = composite_args_.equal_range(url);
  for (; it != end; ++it) {
    auto& [_, nodes] = *it;
    if (nodes.size() != composite_info.num_nodes()) {
      LOGF(ERROR, "Failed to match Node '%s', the number of nodes does not match", name.data());
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (nodes[composite_info.node_index()].expired()) {
      break;
    }
  }
  // No composite arguments exist for the composite driver, create a new set.
  if (it == end) {
    it = composite_args_.emplace(std::move(url), CompositeArgs{composite_info.num_nodes()});
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

  auto client_end = service::ConnectAt<fdh::DriverHost>(endpoints->client);
  if (client_end.is_error()) {
    LOGF(ERROR, "Failed to connect to service '%s': %s",
         fidl::DiscoverableProtocolName<fdh::DriverHost>, client_end.status_string());
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
    if (result.Unwrap_NEW()->is_error()) {
      LOGF(ERROR, "Failed to open exposed directory for component '%s' (%s): %u", name.data(),
           url.data(), result.Unwrap_NEW()->error_value());
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
        if (result.Unwrap_NEW()->is_error()) {
          LOGF(ERROR, "Failed to create component '%s' (%s): %u", name.data(), url.data(),
               result.Unwrap_NEW()->error_value());
          return;
        }
        if (exposed_dir) {
          fdecl::wire::ChildRef child_ref{
              .name = fidl::StringView::FromExternal(name),
              .collection = CollectionName(collection),
          };
          realm_->OpenExposedDir(child_ref, std::move(exposed_dir))
              .ThenExactlyOnce(std::move(open_callback));
        }
      };
  realm_
      ->CreateChild(fdecl::wire::CollectionRef{.name = CollectionName(collection)}, child_decl,
                    child_args)
      .Then(std::move(create_callback));
  return zx::ok();
}
