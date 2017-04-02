// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_subscriber.fidl.h"
#include "apps/maxwell/src/bound_set.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {

// The context graph consists of component nodes and data nodes. Component nodes
// represent Fuchsia components, such as acquirers, agents, and modules. Data
// nodes represent the data they publish and consume. Edges in the graph
// represent dataflow.
//
// TODO(rosswang): Use dataflow edges for traversal to enable coupled type
// conversion and attributed lookup.
// TODO(rosswang): Also represent story structure and allow associative lookup.

class DataNode;

// ComponentNode represents a Fuchsia component, such as an acquirer, agent, or
// module, in the context graph. It tracks data attribution (which data are
// published and consumed by which components).
//
// TODO(rosswang): Track component associations (which components have story-
// graph or composition relations with others, and how).
class ComponentNode {
 public:
  ComponentNode(const std::string& url) : url(url) {}

  // The returned DataNode is owned by this ComponentNode. It is deleted when
  // the ComponentNode is deleted.
  inline DataNode* EmplaceDataNode(const std::string& label);

  const std::string url;

 private:
  // label => entry
  std::unordered_map<std::string, std::unique_ptr<DataNode>> outputs_;

  FIDL_MOVE_ONLY_TYPE(ComponentNode);
};

// DataNode represents a top-level data entry.
//
// The ContextPublisherLink impl could be a separate class, but it is 1:1 with
// the DataNode so it seems reasonable to have them be one and the same.
class DataNode : public ContextPublisherLink {
 public:
  DataNode(ComponentNode* const component, const std::string& label)
      : label(label), component_(component), publisher_(this) {}

  void Update(const fidl::String& json_value) override;
  void Subscribe(ContextSubscriberLinkPtr link);

  void SetPublisher(fidl::InterfaceRequest<ContextPublisherLink> link);

  const std::string label;

 private:
  ComponentNode* const component_;
  std::string json_value_;

  fidl::Binding<ContextPublisherLink> publisher_;
  // We use a BoundPtrSet instead of a fidl::BindingSet because BoundPtrSet
  // supports iteration, which we use in |Update()|.
  BoundPtrSet<ContextSubscriberLink> subscribers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DataNode);
};

inline DataNode* ComponentNode::EmplaceDataNode(const std::string& label) {
  // outputs[label] = DataNode(this, label);
  auto ret = outputs_.emplace(label, std::make_unique<DataNode>(this, label));
  return ret.first->second.get();
}

}  // namespace maxwell
