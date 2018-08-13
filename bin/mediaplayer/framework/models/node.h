// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_NODE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_NODE_H_

#include <atomic>

#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/framework/models/stage.h"
#include "garnet/bin/mediaplayer/framework/packet.h"
#include "garnet/bin/mediaplayer/framework/payload_allocator.h"
#include "garnet/bin/mediaplayer/framework/refs.h"

namespace media_player {

class GenericNode {
 public:
  virtual ~GenericNode() {}

  // Sets the generic stage. This method is generally only called by the graph.
  void SetGenericStage(Stage* generic_stage) { generic_stage_ = generic_stage; }

  // Gets the generic stage. This method is generally only called by the graph.
  Stage* generic_stage() const { return generic_stage_; }

  // Returns a diagnostic label for the node.
  virtual const char* label() const;

  // Generates a report for the node.
  virtual void Dump(std::ostream& os) const;

 protected:
  // Posts a task to run as soon as possible. A task posted with this method is
  // run exclusive of any other such tasks.
  void PostTask(fit::closure task);

 private:
  std::atomic<Stage*> generic_stage_;
};

// Base class for all nodes.
template <typename TStage>
class Node : public GenericNode {
 public:
  ~Node() override {}

  // Sets |stage_|. This method is called only by the graph and the stage.
  void SetStage(TStage* stage) { SetGenericStage(stage); }

 protected:
  // Returns a pointer to the stage for this node. Returns nullptr if the stage
  // has been destroyed.
  TStage* stage() const { return reinterpret_cast<TStage*>(generic_stage()); }
};

// Provides a means of determining the stage implementation type for a given
// node type. This template should be specialized for each node model defined.
// For example, if we've defined a node model Foo that uses the stage
// implementation FooStageImpl, we'd do this:
//
//   template <typename TNode>
//   struct NodeTraits<
//       TNode,
//       typename std::enable_if<std::is_base_of<Foo, TNode>::value>::type> {
//     using stage_impl_type = FooStageImpl;
//   };
//
template <typename TNode, typename Enable = void>
struct NodeTraits {
  using stage_impl_type = void;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_NODE_H_
