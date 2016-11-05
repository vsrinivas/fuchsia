// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/context_engine.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

#include "apps/maxwell/bound_set.h"

namespace maxwell {
namespace context_engine {

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
  inline DataNode* EmplaceDataNode(const std::string& label,
                                   const std::string& schema);

  const std::string url;

 private:
  // label => schema => entry
  std::unordered_map<std::string, std::unordered_map<std::string, DataNode>>
      outputs_;

  FIDL_MOVE_ONLY_TYPE(ComponentNode);
};

// DataNode represents a top-level schema'd datum.
//
// TOOD(rosswang): Allow decomposed and fuzzy lookup.
//
// The ContextPublisherLink impl could be a separate class, but it is 1:1 with
// the DataNode so it seems reasonable to have them be one and the same.
class DataNode : public ContextPublisherLink {
 public:
  DataNode(ComponentNode* const component,
           const std::string& label,
           const std::string& schema)
      : label(label),
        schema(schema),
        component_(component),
        publisher_(this),
        subscribers_(this) {}

  void Update(const fidl::String& json_value) override;
  void Subscribe(ContextSubscriberLinkPtr link);

  void SetPublisher(
      fidl::InterfaceHandle<ContextPublisherController> controller,
      fidl::InterfaceRequest<ContextPublisherLink> link);

  const std::string label;
  const std::string schema;

 private:
  class SubscriberSet : public BoundPtrSet<ContextSubscriberLink> {
   public:
    SubscriberSet(DataNode* node) : node_(node) {}

   protected:
    void OnConnectionError(ContextSubscriberLink* interface_ptr) override;

   private:
    DataNode* node_;
  };

  ComponentNode* const component_;
  std::string json_value_;

  ContextPublisherControllerPtr publisher_controller_;
  fidl::Binding<ContextPublisherLink> publisher_;
  SubscriberSet subscribers_;

  FIDL_MOVE_ONLY_TYPE(DataNode);
};

inline DataNode* ComponentNode::EmplaceDataNode(const std::string& label,
                                                const std::string& schema) {
  // outputs[label][schema] = DataNode(this, label, schema);
  return &outputs_[label]
              .emplace(std::piecewise_construct, std::forward_as_tuple(schema),
                       std::forward_as_tuple(this, label, schema))
              .first->second;
}

}  // namespace context_engine
}  // namespace maxwell
