// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_ASYNC_NODE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_ASYNC_NODE_H_

#include "garnet/bin/mediaplayer/framework/models/node.h"
#include "garnet/bin/mediaplayer/framework/models/stage.h"
#include "garnet/bin/mediaplayer/framework/packet.h"
#include "garnet/bin/mediaplayer/framework/payload_allocator.h"

namespace media_player {

// Stage for |AsyncNode|.
class AsyncNodeStage : public Stage {
 public:
  ~AsyncNodeStage() override {}

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to inputs (inbound packets from upstream).
  //////////////////////////////////////////////////////////////////////////////

  // Requests an input packet on the specified input. |input_index| must be
  // less than the configured input count. This method may be called from
  // |AsyncNode::PutInputPacket|.
  //
  // This method may be called on an arbitrary thread.
  virtual void RequestInputPacket(size_t input_index = 0) = 0;

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to outputs (outbound packets to downstream).
  //////////////////////////////////////////////////////////////////////////////

  // Supplies a packet to be sent downstream on the specified output.
  //
  // This method may be called on an arbitrary thread.
  virtual void PutOutputPacket(PacketPtr packet, size_t output_index = 0) = 0;
};

// Node model for async nodes. This model is intended to replace all other
// async models.
// TODO(dalesat): Remove other async models.
class AsyncNode : public Node<AsyncNodeStage> {
 public:
  ~AsyncNode() override {}

  // Gets the number of inputs and outputs this node will have.
  //
  // This method will be called on the graph's thread.
  //
  // TODO(dalesat): Combine this with SetStage/SetGenericStage.
  // TODO(dalesat): Support dynamic reconfiguration.
  virtual void GetConfiguration(size_t* input_count, size_t* output_count) = 0;

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to inputs (inbound packets from upstream).
  //////////////////////////////////////////////////////////////////////////////

  // Flushes an input. |hold_frame| indicates whether a video renderer should
  // hold and display the newest frame. The callback is used to indicate that
  // the flush operation is complete. It may be called synchronously or on an
  // arbitrary thread. The default implementation aborts.
  //
  // Flushing operations proceed downstream from a particular output until a
  // sink (node with no outputs) is reached. When an input is flushed on a node
  // that has outputs, the node in question can assume that all of its outputs
  // will be flushed as well. Outputs may be flushed independently, so the
  // converse it not true.
  //
  // This method will be called on the graph's thread.
  virtual void FlushInput(bool hold_frame, size_t input_index,
                          fit::closure callback) {
    FXL_CHECK(false) << "FlushInput not implemented.";
  }

  // Gets an allocator that must be used for input packets on the specified
  // input or nullptr if there's no such requirement. The default implementation
  // returns nullptr.
  //
  // This method will be called on the graph's thread.
  virtual std::shared_ptr<PayloadAllocator> allocator_for_input(
      size_t input_index) {
    return nullptr;
  }

  // Supplies the node with a packet that arrived on the specified input. This
  // method may call |AsyncNodeStage::RequestInputPacket|. The default
  // implementation aborts.
  //
  // This method will be called on the graph's thread.
  virtual void PutInputPacket(PacketPtr packet, size_t input_index) {
    FXL_CHECK(false) << "PutInputPacket not implemented.";
  }

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to outputs (outbound packets to downstream).
  //////////////////////////////////////////////////////////////////////////////

  // Flushes an output. The callback is used to indicate that the flush
  // operation is complete. It may be called synchronously or on an arbitrary
  // thread. The default implementation aborts.
  //
  // Flushing operations proceed downstream from a particular output until a
  // sink (node with no outputs) is reached. When an input is flushed on a node
  // that has outputs, the node in question can assume that all of its outputs
  // will be flushed as well. Outputs may be flushed independently, so the
  // converse it not true.
  //
  // This method will be called on the graph's thread.
  virtual void FlushOutput(size_t output_index, fit::closure callback) {
    FXL_CHECK(false) << "FlushOutput not implemented.";
  }

  // Whether the node can accept an allocator to use for output packets from
  // the specifed output. The default implementation returns false.
  //
  // This method will be called on the graph's thread.
  virtual bool can_accept_allocator_for_output(size_t output_index) const {
    return false;
  }

  // Sets the allocator the node must use for output packets for the specified
  // output. This method is never called if |can_accept_allocator| returns
  // false for the output. The default implementation aborts.
  //
  // This method will be called on the graph's thread.
  virtual void SetAllocatorForOutput(
      std::shared_ptr<PayloadAllocator> allocator, size_t output_index) {
    FXL_CHECK(false) << "SetAllocator not implemented.";
  }

  // Requests an output packet. The default implementation aborts.
  //
  // This method will be called on the graph's thread.
  virtual void RequestOutputPacket() {
    FXL_CHECK(false) << "RequestOutputPacket not implemented.";
  }
};

class AsyncNodeStageImpl;

template <typename TNode>
struct NodeTraits<TNode, typename std::enable_if<
                             std::is_base_of<AsyncNode, TNode>::value>::type> {
  using stage_impl_type = AsyncNodeStageImpl;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_MODELS_ASYNC_NODE_H_
