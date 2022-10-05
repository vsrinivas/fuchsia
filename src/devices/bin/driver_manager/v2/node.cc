// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/node.h"

#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <lib/driver2/node_add_args.h>
#include <lib/driver2/start_args.h>

#include <deque>
#include <unordered_set>

#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace fdf = fuchsia_driver_framework;
namespace fdecl = fuchsia_component_decl;

namespace dfv2 {

namespace {

// The driver's component name is based on the node name, which means that the
// node name cam only have [a-z0-9-_.] characters. DFv1 composites contain ':'
// which is not allowed, so replace those characters.
void TransformToValidName(std::string& name) {
  std::replace(name.begin(), name.end(), ':', '_');
  std::replace(name.begin(), name.end(), '/', '.');
}

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

bool IsDefaultOffer(std::string_view target_name) {
  return std::string_view("default").compare(target_name) == 0;
}

Node* PrimaryParent(const std::vector<Node*>& parents) {
  return parents.empty() ? nullptr : parents[0];
}

template <typename T>
void UnbindAndReset(std::optional<fidl::ServerBindingRef<T>>& ref) {
  if (ref) {
    ref->Unbind();
    ref.reset();
  }
}

fit::result<fdf::wire::NodeError> ValidateSymbols(fidl::VectorView<fdf::wire::NodeSymbol> symbols) {
  std::unordered_set<std::string_view> names;
  for (auto& symbol : symbols) {
    if (!symbol.has_name()) {
      LOGF(ERROR, "SymbolError: a symbol is missing a name");
      return fit::error(fdf::wire::NodeError::kSymbolNameMissing);
    }
    if (!symbol.has_address()) {
      LOGF(ERROR, "SymbolError: symbol '%.*s' is missing an address",
           static_cast<int>(symbol.name().size()), symbol.name().data());
      return fit::error(fdf::wire::NodeError::kSymbolAddressMissing);
    }
    auto [_, inserted] = names.emplace(symbol.name().get());
    if (!inserted) {
      LOGF(ERROR, "SymbolError: symbol '%.*s' already exists",
           static_cast<int>(symbol.name().size()), symbol.name().data());
      return fit::error(fdf::wire::NodeError::kSymbolAlreadyExists);
    }
  }
  return fit::ok();
}

}  // namespace

std::optional<fdecl::wire::Offer> CreateCompositeServiceOffer(fidl::AnyArena& arena,
                                                              fdecl::wire::Offer& offer,
                                                              std::string_view parents_name,
                                                              bool primary_parent) {
  if (!offer.is_service() || !offer.service().has_source_instance_filter() ||
      !offer.service().has_renamed_instances()) {
    return std::nullopt;
  }

  size_t new_instance_count = offer.service().renamed_instances().count();
  if (primary_parent) {
    for (auto& instance : offer.service().renamed_instances()) {
      if (IsDefaultOffer(instance.target_name.get())) {
        new_instance_count++;
      }
    }
  }

  size_t new_filter_count = offer.service().source_instance_filter().count();
  if (primary_parent) {
    for (auto& filter : offer.service().source_instance_filter()) {
      if (IsDefaultOffer(filter.get())) {
        new_filter_count++;
      }
    }
  }

  // We have to create a new offer so we aren't manipulating our parent's offer.
  auto service = fdecl::wire::OfferService::Builder(arena);
  if (offer.service().has_source_name()) {
    service.source_name(offer.service().source_name());
  }
  if (offer.service().has_target_name()) {
    service.target_name(offer.service().target_name());
  }
  if (offer.service().has_source()) {
    service.source(offer.service().source());
  }
  if (offer.service().has_target()) {
    service.target(offer.service().target());
  }

  size_t index = 0;
  fidl::VectorView<fdecl::wire::NameMapping> mappings(arena, new_instance_count);
  for (auto instance : offer.service().renamed_instances()) {
    // The instance is not "default", so copy it over.
    if (!IsDefaultOffer(instance.target_name.get())) {
      mappings[index].source_name = fidl::StringView(arena, instance.source_name.get());
      mappings[index].target_name = fidl::StringView(arena, instance.target_name.get());
      index++;
      continue;
    }

    // We are the primary parent, so add the "default" offer.
    if (primary_parent) {
      mappings[index].source_name = fidl::StringView(arena, instance.source_name.get());
      mappings[index].target_name = fidl::StringView(arena, instance.target_name.get());
      index++;
    }

    // Rename the instance to match the parent's name.
    mappings[index].source_name = fidl::StringView(arena, instance.source_name.get());
    mappings[index].target_name = fidl::StringView(arena, parents_name);
    index++;
  }
  ZX_ASSERT(index == new_instance_count);

  index = 0;
  fidl::VectorView<fidl::StringView> filters(arena, new_instance_count);
  for (auto filter : offer.service().source_instance_filter()) {
    // The filter is not "default", so copy it over.
    if (!IsDefaultOffer(filter.get())) {
      filters[index] = fidl::StringView(arena, filter.get());
      index++;
      continue;
    }

    // We are the primary parent, so add the "default" filter.
    if (primary_parent) {
      filters[index] = fidl::StringView(arena, "default");
      index++;
    }

    // Rename the filter to match the parent's name.
    filters[index] = fidl::StringView(arena, parents_name);
    index++;
  }
  ZX_ASSERT(index == new_filter_count);

  service.renamed_instances(mappings);
  service.source_instance_filter(filters);

  return fdecl::wire::Offer::WithService(arena, service.Build());
}

std::optional<fdecl::wire::Offer> CreateCompositeOffer(fidl::AnyArena& arena,
                                                       fdecl::wire::Offer& offer,
                                                       std::string_view parents_name,
                                                       bool primary_parent) {
  // We route 'service' capabilities based on the parent's name.
  if (offer.is_service()) {
    return CreateCompositeServiceOffer(arena, offer, parents_name, primary_parent);
  }

  // Other capabilities we can simply forward unchanged, but allocated on the new arena.
  return fidl::ToWire(arena, fidl::ToNatural(offer));
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

Node::Node(std::string_view name, std::vector<Node*> parents, NodeManager* node_manager,
           async_dispatcher_t* dispatcher)
    : devfs_name_(name),
      name_(name),
      parents_(std::move(parents)),
      node_manager_(node_manager),
      dispatcher_(dispatcher) {
  TransformToValidName(name_);
  if (auto primary_parent = PrimaryParent(parents_)) {
    // By default, we set `driver_host_` to match the primary parent's
    // `driver_host_`. If the node is then subsequently bound to a driver in a
    // different driver host, this value will be updated to match.
    driver_host_ = primary_parent->driver_host_;
  }
}

Node::Node(std::string_view name, std::vector<Node*> parents, NodeManager* node_manager,
           async_dispatcher_t* dispatcher, DriverHost* driver_host)
    : devfs_name_(name),
      name_(name),
      parents_(std::move(parents)),
      node_manager_(node_manager),
      dispatcher_(dispatcher),
      driver_host_(driver_host) {
  TransformToValidName(name_);
}

zx::status<std::shared_ptr<Node>> Node::CreateCompositeNode(
    std::string_view node_name, std::vector<Node*> parents, std::vector<std::string> parents_names,
    std::vector<fuchsia_driver_framework::wire::NodeProperty> properties,
    NodeManager* driver_binder, async_dispatcher_t* dispatcher) {
  auto composite = std::make_shared<Node>(node_name, std::move(parents), driver_binder, dispatcher);
  composite->parents_names_ = std::move(parents_names);

  for (auto& prop : properties) {
    auto natural = fidl::ToNatural(prop);
    auto new_prop = fidl::ToWire(composite->arena_, std::move(natural));
    composite->properties_.push_back(new_prop);
  }

  auto primary = PrimaryParent(composite->parents_);
  // We know that our device has a parent because we're creating it.
  ZX_ASSERT(primary);

  // Copy the symbols from the primary parent.
  composite->symbols_.reserve(primary->symbols_.size());
  for (auto& symbol : primary->symbols_) {
    composite->symbols_.emplace_back(fdf::wire::NodeSymbol::Builder(composite->arena_)
                                         .name(composite->arena_, symbol.name().get())
                                         .address(symbol.address())
                                         .Build());
  }

  // Copy the offers from each parent.
  std::vector<fdecl::wire::Offer> node_offers;
  size_t parent_index = 0;
  for (const Node* parent : composite->parents_) {
    auto parent_offers = parent->offers();
    node_offers.reserve(node_offers.size() + parent_offers.count());

    for (auto& parent_offer : parent_offers) {
      auto offer = CreateCompositeOffer(composite->arena_, parent_offer,
                                        composite->parents_names_[parent_index], parent_index == 0);
      if (offer) {
        node_offers.push_back(*offer);
      }
    }
    parent_index++;
  }
  composite->offers_ = std::move(node_offers);

  composite->AddToParents();
  return zx::ok(std::move(composite));
}

Node::~Node() { UnbindAndReset(controller_ref_); }

const std::string& Node::name() const { return name_; }

const DriverComponent* Node::driver_component() const { return driver_component_.get(); }

const std::vector<Node*>& Node::parents() const { return parents_; }

const std::list<std::shared_ptr<Node>>& Node::children() const { return children_; }

fidl::VectorView<fuchsia_component_decl::wire::Offer> Node::offers() const {
  // TODO(fxbug.dev/66150): Once FIDL wire types support a Clone() method,
  // remove the const_cast.
  return fidl::VectorView<fdecl::wire::Offer>::FromExternal(
      const_cast<decltype(offers_)&>(offers_));
}

fidl::VectorView<fdf::wire::NodeSymbol> Node::symbols() const {
  // TODO(fxbug.dev/7999): Remove const_cast once VectorView supports const.
  return fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(
      const_cast<decltype(symbols_)&>(symbols_));
}

const std::vector<fdf::wire::NodeProperty>& Node::properties() const { return properties_; }

void Node::set_collection(Collection collection) { collection_ = collection; }

std::string Node::TopoName() const {
  std::deque<std::string_view> names;
  for (auto node = this; node != nullptr; node = PrimaryParent(node->parents_)) {
    names.push_front(node->name());
  }
  return fxl::JoinStrings(names, ".");
}

fuchsia_driver_framework::wire::NodeAddArgs Node::CreateAddArgs(fidl::AnyArena& arena) {
  fuchsia_driver_framework::wire::NodeAddArgs args(arena);
  args.set_name(arena, arena, name());
  args.set_offers(arena, offers());
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
  // Get an extra shared_ptr to ourselves so we are not freed halfway through this function.
  auto this_node = shared_from_this();

  // Disable driver binding for the node. This also prevents child nodes from
  // being added to this node.
  node_manager_ = nullptr;

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
  if (driver_component_ && driver_component_->is_alive()) {
    driver_component_->StopDriver();
    return;
  }

  // Let the removal begin

  // Erase ourselves from each parent.
  for (auto parent : parents_) {
    auto& children = parent->children_;
    children.erase(std::find(children.begin(), children.end(), this_node));

    // If our parent is waiting to be removed and we are its last child,
    // then remove it.
    if (parent->removal_in_progress_ && children.empty()) {
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

fit::result<fuchsia_driver_framework::wire::NodeError, std::shared_ptr<Node>> Node::AddChild(
    fuchsia_driver_framework::wire::NodeAddArgs args,
    fidl::ServerEnd<fuchsia_driver_framework::NodeController> controller,
    fidl::ServerEnd<fuchsia_driver_framework::Node> node) {
  if (node_manager_ == nullptr) {
    LOGF(WARNING, "Failed to add Node, as this Node '%s' was removed", name().data());
    return fit::as_error(fdf::wire::NodeError::kNodeRemoved);
  }
  if (!args.has_name()) {
    LOGF(ERROR, "Failed to add Node, a name must be provided");
    return fit::as_error(fdf::wire::NodeError::kNameMissing);
  }
  auto name = args.name().get();
  if (name.find('.') != std::string_view::npos) {
    LOGF(ERROR, "Failed to add Node '%.*s', name must not contain '.'",
         static_cast<int>(name.size()), name.data());
    return fit::as_error(fdf::wire::NodeError::kNameInvalid);
  }
  for (auto& child : children_) {
    if (child->name() == name) {
      LOGF(ERROR, "Failed to add Node '%.*s', name already exists among siblings",
           static_cast<int>(name.size()), name.data());
      return fit::as_error(fdf::wire::NodeError::kNameAlreadyExists);
    }
  };
  auto child = std::make_shared<Node>(name, std::vector<Node*>{this}, *node_manager_, dispatcher_);

  if (args.has_offers()) {
    child->offers_.reserve(args.offers().count());
    for (auto& offer : args.offers()) {
      auto has_source_name =
          VisitOffer<bool>(offer, [](auto& decl) { return decl.has_source_name(); });
      if (!has_source_name.value_or(false)) {
        LOGF(ERROR, "Failed to add Node '%.*s', an offer must have a source name",
             static_cast<int>(name.size()), name.data());
        return fit::as_error(fdf::wire::NodeError::kOfferSourceNameMissing);
      }
      auto has_ref = VisitOffer<bool>(
          offer, [](auto& decl) { return decl.has_source() || decl.has_target(); });
      if (has_ref.value_or(false)) {
        LOGF(ERROR, "Failed to add Node '%.*s', an offer must not have a source or target",
             static_cast<int>(name.size()), name.data());
        return fit::as_error(fdf::wire::NodeError::kOfferRefExists);
      }

      // Find a parent node with a collection. This indicates that a driver has
      // been bound to the node, and the driver is running within the collection.
      auto source_node = this;
      while (source_node && source_node->collection_ == Collection::kNone) {
        source_node = PrimaryParent(source_node->parents_);
      }
      VisitOffer<bool>(offer, [&arena = child->arena_, source_node](auto& decl) mutable {
        // Assign the source of the offer.
        fdecl::wire::ChildRef source_ref{
            .name = {arena, source_node->TopoName()},
            .collection = CollectionName(source_node->collection_),
        };
        decl.set_source(arena, fdecl::wire::Ref::WithChild(arena, source_ref));
        return true;
      });

      auto natural = fidl::ToNatural(offer);
      child->offers_.push_back(fidl::ToWire(child->arena_, std::move(natural)));
    }
  }

  if (args.has_properties()) {
    child->properties_.reserve(args.properties().count() + 1);  // + 1 for DFv2 prop.
    for (auto& property : args.properties()) {
      child->properties_.emplace_back(fidl::ToWire(child->arena_, fidl::ToNatural(property)));
    }
  }

  // We set a property for DFv2 devices.
  child->properties_.emplace_back(
      driver::MakeProperty(child->arena_, "fuchsia.driver.framework.dfv2", true));

  if (args.has_symbols()) {
    auto is_valid = ValidateSymbols(args.symbols());
    if (is_valid.is_error()) {
      LOGF(ERROR, "Failed to add Node '%.*s', bad symbols", static_cast<int>(name.size()),
           name.data());
      return fit::as_error(is_valid.error_value());
    }

    child->symbols_.reserve(args.symbols().count());
    for (auto& symbol : args.symbols()) {
      child->symbols_.emplace_back(fdf::wire::NodeSymbol::Builder(child->arena_)
                                       .name(child->arena_, symbol.name().get())
                                       .address(symbol.address())
                                       .Build());
    }
  }

  if (controller.is_valid()) {
    child->controller_ref_ = fidl::BindServer<fidl::WireServer<fdf::NodeController>>(
        dispatcher_, std::move(controller), child.get());
  }
  if (node.is_valid()) {
    child->node_ref_ = fidl::BindServer<fidl::WireServer<fdf::Node>>(
        dispatcher_, std::move(node), child,
        [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
  } else {
    // We don't care about tracking binds here, sending nullptr is fine.
    (*node_manager_)->Bind(*child, nullptr);
  }
  child->AddToParents();
  return fit::ok(child);
}

bool Node::IsComposite() const { return parents_.size() > 1; }

void Node::Remove(RemoveCompleter::Sync& completer) { Remove(); }

void Node::AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) {
  auto node = AddChild(request->args, std::move(request->controller), std::move(request->node));
  if (node.is_error()) {
    completer.Reply(node.take_error());
    return;
  }
  completer.ReplySuccess();
}

zx::status<> Node::StartDriver(
    fuchsia_component_runner::wire::ComponentStartInfo start_info,
    fidl::ServerEnd<fuchsia_component_runner::ComponentController> controller) {
  auto url = start_info.resolved_url().get();
  bool colocate = driver::ProgramValue(start_info.program(), "colocate").value_or("") == "true";

  if (colocate && !driver_host_) {
    LOGF(ERROR,
         "Failed to start driver '%.*s', driver is colocated but does not have a prent with a "
         "driver host",
         static_cast<int>(url.size()), url.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto symbols = fidl::VectorView<fdf::wire::NodeSymbol>();
  if (colocate) {
    symbols = this->symbols();
  }

  // Launch a driver host if we are not colocated.
  if (!colocate) {
    auto result = (*node_manager_)->CreateDriverHost();
    if (result.is_error()) {
      return result.take_error();
    }
    driver_host_ = result.value();
  }

  // Bind the Node associated with the driver.
  auto endpoints = fidl::CreateEndpoints<fdf::Node>();
  if (endpoints.is_error()) {
    return zx::error(endpoints.error_value());
  }
  node_ref_ = fidl::BindServer<fidl::WireServer<fdf::Node>>(
      dispatcher_, std::move(endpoints->server), shared_from_this(),
      [](fidl::WireServer<fdf::Node>* node, auto, auto) { static_cast<Node*>(node)->Remove(); });

  LOGF(INFO, "Binding %.*s to  %s", static_cast<int>(url.size()), url.data(), name().c_str());
  // Start the driver within the driver host.
  auto start = (*driver_host_)
                   ->Start(std::move(endpoints->client), devfs_name_, std::move(symbols),
                           std::move(start_info));
  if (start.is_error()) {
    return zx::error(start.error_value());
  }

  // Create a DriverComponent to manage the driver.
  driver_component_ = std::make_unique<DriverComponent>(
      std::move(*start), std::move(controller), dispatcher_, url,
      [node = this](auto status) { node->Remove(); },
      [node = this](auto status) { node->Remove(); });
  return zx::ok();
}

}  // namespace dfv2
