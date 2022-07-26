// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/node.h"

#include <fidl/fuchsia.driver.framework/cpp/fidl.h>

#include <deque>
#include <unordered_set>

#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace fdf = fuchsia_driver_framework;
namespace fdecl = fuchsia_component_decl;

namespace dfv2 {

namespace {

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

fitx::result<fdf::wire::NodeError> ValidateSymbols(
    fidl::VectorView<fdf::wire::NodeSymbol> symbols) {
  std::unordered_set<std::string_view> names;
  for (auto& symbol : symbols) {
    if (!symbol.has_name()) {
      LOGF(ERROR, "SymbolError: a symbol is missing a name");
      return fitx::error(fdf::wire::NodeError::kSymbolNameMissing);
    }
    if (!symbol.has_address()) {
      LOGF(ERROR, "SymbolError: symbol '%.*s' is missing an address",
           static_cast<int>(symbol.name().size()), symbol.name().data());
      return fitx::error(fdf::wire::NodeError::kSymbolAddressMissing);
    }
    auto [_, inserted] = names.emplace(symbol.name().get());
    if (!inserted) {
      LOGF(ERROR, "SymbolError: symbol '%.*s' already exists",
           static_cast<int>(symbol.name().size()), symbol.name().data());
      return fitx::error(fdf::wire::NodeError::kSymbolAlreadyExists);
    }
  }
  return fitx::ok();
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
  // The driver's component name is based on the node name, which means that the
  // node name cam only have [a-z0-9-_.] characters. DFv1 composites contain ':'
  // which is not allowed, so replace those characters.
  std::replace(name_.begin(), name_.end(), ':', '_');

  if (auto primary_parent = PrimaryParent(parents_)) {
    // By default, we set `driver_host_` to match the primary parent's
    // `driver_host_`. If the node is then subsequently bound to a driver in a
    // different driver host, this value will be updated to match.
    driver_host_ = primary_parent->driver_host_;
  }
}

zx::status<std::shared_ptr<Node>> Node::CreateCompositeNode(
    std::string_view node_name, std::vector<Node*> parents, std::vector<std::string> parents_names,
    std::vector<fuchsia_driver_framework::wire::NodeProperty> properties,
    DriverBinder* driver_binder, async_dispatcher_t* dispatcher) {
  auto composite = std::make_shared<Node>(node_name, std::move(parents), driver_binder, dispatcher);

  for (auto& prop : properties) {
    auto natural = fidl::ToNatural(prop);
    auto new_prop = fidl::ToWire(composite->arena_, std::move(natural));
    composite->properties_.push_back(new_prop);
  }

  composite->parents_names_ = std::move(parents_names);
  composite->AddToParents();
  return zx::ok(std::move(composite));
}

Node::~Node() { UnbindAndReset(controller_ref_); }

const std::string& Node::name() const { return name_; }

const DriverComponent* Node::driver_component() const { return driver_component_.get(); }

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
    auto& symbols = IsComposite() ? primary_parent->symbols_ : symbols_;
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

void Node::set_driver_component(std::unique_ptr<DriverComponent> driver_component) {
  driver_component_ = std::move(driver_component);
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
  for (const Node* parent : parents_) {
    // Find a parent node with a collection. This indicates that a driver has
    // been bound to the node, and the driver is running within the collection.
    auto source_node = parent;
    for (; source_node != nullptr && source_node->collection_ == Collection::kNone;
         source_node = PrimaryParent(source_node->parents_)) {
    }
    // If this is a composite node, then the offers come from the parent nodes.
    auto& parent_offers = IsComposite() ? parent->offers() : offers();
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
      if (IsComposite() && offer.is_directory()) {
        auto new_offer = CreateCompositeDirOffer(arena, offer, parents_names_[parent_index]);
        if (new_offer) {
          node_offers.push_back(*new_offer);

          // If we aren't the primary parent, then skip adding the "default" directory.
          if (parent_index != 0) {
            continue;
          }
        }
      }

      // If we are a composite node, then we route 'service' directories based on the parent's name.
      if (IsComposite() && offer.is_service()) {
        auto new_offer = CreateCompositeServiceOffer(arena, offer, parents_names_[parent_index],
                                                     parent_index == 0);
        if (new_offer) {
          node_offers.push_back(*new_offer);
        }
        continue;
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
  if (driver_component_ && driver_component_->is_alive()) {
    driver_component_->StopDriver();
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

bool Node::IsComposite() const { return parents_.size() > 1; }

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
      child->properties_.emplace_back(fidl::ToWire(child->arena_, fidl::ToNatural(property)));
    }
  }

  // We set a property for DFv2 devices.
  child->properties_.emplace_back(fdf::wire::NodeProperty::Builder(child->arena_)
                                      .key(fdf::wire::NodePropertyKey::WithStringValue(
                                          child->arena_, "fuchsia.driver.framework.dfv2"))
                                      .value(fdf::wire::NodePropertyValue::WithBoolValue(true))
                                      .Build());

  if (request->args.has_symbols()) {
    auto is_valid = ValidateSymbols(request->args.symbols());
    if (is_valid.is_error()) {
      LOGF(ERROR, "Failed to add Node '%.*s', bad symbols", static_cast<int>(name.size()),
           name.data());
      completer.ReplyError(is_valid.error_value());
      return;
    }

    child->symbols_.reserve(request->args.symbols().count());
    for (auto& symbol : request->args.symbols()) {
      child->symbols_.emplace_back(fdf::wire::NodeSymbol::Builder(child->arena_)
                                       .name(child->arena_, symbol.name().get())
                                       .address(symbol.address())
                                       .Build());
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

}  // namespace dfv2
