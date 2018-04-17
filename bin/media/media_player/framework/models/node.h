// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_NODE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_NODE_H_

#include <atomic>

#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"
#include "lib/fxl/functional/closure.h"

namespace media_player {

class GenericNode {
 public:
  virtual ~GenericNode() {}

  void SetGenericStage(Stage* generic_stage) { generic_stage_ = generic_stage; }

  Stage* generic_stage() { return generic_stage_; }

  virtual const char* label() const { return "<not labelled>"; }

 protected:
  // Posts a task to run as soon as possible. A task posted with this method is
  // run exclusive of any other such tasks.
  void PostTask(const fxl::Closure& task) {
    Stage* generic_stage = generic_stage_;
    if (generic_stage) {
      generic_stage->PostTask(task);
    }
  }

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
  TStage* stage() { return reinterpret_cast<TStage*>(generic_stage()); }
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_NODE_H_
