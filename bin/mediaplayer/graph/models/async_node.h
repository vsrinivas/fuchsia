// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_MODELS_ASYNC_NODE_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_MODELS_ASYNC_NODE_H_

#include "garnet/bin/mediaplayer/graph/models/node.h"
#include "garnet/bin/mediaplayer/graph/models/stage.h"
#include "garnet/bin/mediaplayer/graph/packet.h"
#include "garnet/bin/mediaplayer/graph/payloads/payload_allocator.h"
#include "garnet/bin/mediaplayer/graph/payloads/payload_config.h"

namespace media_player {

// TODO(dalesat): Ensure that we contractually have all the configuration
// info we need.
// TODO(dalesat): Track payload allocations and complain when usage exceeds
// expectations set by payload configurations.
// TODO(dalesat): Be more precise about the language around the semantics of
// payload configurations.

// Stage for |AsyncNode|.
class AsyncNodeStage : public Stage {
 public:
  using AllocateCallback =
      fit::function<fbl::RefPtr<PayloadBuffer>(uint64_t, const PayloadVmos&)>;

  ~AsyncNodeStage() override {}

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to inputs (inbound packets from upstream).
  //////////////////////////////////////////////////////////////////////////////

  // Indicates that the specified input exists but explicityly defers its
  // configuration until a later time. This call is provided so the stage is
  // informed that the input exists, even though the node doesn't know enough
  // at that point to configure the input completely. This allows the input to
  // be connected up by whoever is building the graph. The connection won't
  // transition to ready state (see |InputConnectionReady|) until the input is
  // fully configured.
  //
  // This method must be called on the main graph thread.
  virtual void ConfigureInputDeferred(size_t input_index = 0) = 0;

  // Configures an input to address payloads as contiguous regions of process
  // virtual memory. |max_aggregate_payload_size| sets expectations about how
  // much memory will be required for all the payloads that the input will keep
  // in memory at one time. This value does not include memory required by
  // the connected output or for buffers queued on the connection. Likewise,
  // |max_payload_count| sets expectations about how many payloads the input
  // will keep in memory at one time. At least one of these two values must be
  // non-zero.
  //
  // Calling this function prohibits the use of |UseInputVmos| or
  // |ProvideInputVmos| for the specified input.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::InputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureInputToUseLocalMemory(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
      size_t input_index = 0) = 0;

  // Configures an input to address payloads as contiguous regions in VMOs
  // that are created by some other party. |max_aggregate_payload_size| sets
  // expectations about how much memory will be required for the payloads that
  // the input will keep in memory at one time. This value does not include
  // memory required by the connected output or for buffers queued on the
  // connection. Likewise, |max_payload_count| sets expectations about how many
  // payloads the input will keep in memory at one time. |max_payload_size|
  // sets expectations about how large payloads can be. Either or both of
  // |max_aggregate_payload_size| and |max_payload_count| must be non-zero.
  //
  // |vmo_allocation| indicates how the payload buffers must be distributed
  // across the VMOs. |physically_contiguous| indicates whether the VMOs
  // must be physically contiguous. If and only if |physically_contiguous|
  // is true, |bti_handle| provides the handle required for
  // |zx_vmo_create_contiguous|.
  //
  // Calling this function prohibits the use of |ProvideInputVmos| for the
  // specified input. |UseInputVmos| is available to determine what VMOs are
  // being used.
  //
  // |allocate_callback| is used when the node wants to perform allocations
  // against the VMOs itself rather than allowing the |VmoAllocator| to do
  // it. This callback will be called on an arbitrary thread and may not
  // call any methods on the stage. The VMOs the allocator callback must
  // allocate from will be provided by the payload manager, not by the
  // connected output. This guarantee is made so the input doesn't have to
  // deal with the arbitrary VMOs provided by the output.
  // TODO(dalesat): Be explicit about what the VMOs will actually be like.
  //
  // Returns true if the connection is ready for allocation activity.
  // Returns false if not, in which case |AsyncNode::InputConnectionReady|
  // is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|).
  // Otherwise, it must be called on the main graph thread.
  virtual bool ConfigureInputToUseVmos(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
      uint64_t max_payload_size, VmoAllocation vmo_allocation,
      bool physically_contiguous = false, zx::handle bti_handle = zx::handle(),
      AllocateCallback allocate_callback = nullptr, size_t input_index = 0) = 0;

  // Configures an input to address payloads as contiguous regions in VMOs
  // that the input provides. If the VMOs provided by the input are
  // inadequate to hold all the payloads that are kept in memory at one time,
  // the connection will adapt accordingly by creating a separate allocator
  // for the output and doing copies. |vmo_allocation| indicates how the
  // payload buffers will be distributed across the VMOs.
  // |physically_contiguous| indicates whether the VMOs will be contiguous in
  // physical memory.
  //
  // Calling this function allows the use of |ProvideInputVmos| for the
  // specified input.
  //
  // |allocate_callback| is used when the node wants to perform allocations
  // against the VMOs itself rather than allowing the |VmoAllocator| to do it.
  // This callback will be called on an arbitrary thread and may not call any
  // methods on the stage. The VMOs the allocator callback must allocate from
  // will always be the same VMOs provided by the input.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::InputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureInputToProvideVmos(
      VmoAllocation vmo_allocation, bool physically_contiguous = false,
      AllocateCallback allocate_callback = nullptr, size_t input_index = 0) = 0;

  // Returns true if the specified output is ready for calls to |UseInputVmos|
  // or |ProvideInputVmos|.
  //
  // This method may be called on an arbitrary thread.
  virtual bool InputConnectionReady(size_t input_index = 0) const = 0;

  // Returns the |PayloadVmos| for the specified input. This method is only
  // is only useable if |ConfigureInputToUseVmos| or
  // |ConfigureInputToProvideVmos| has been called to configure the specified
  // input, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  virtual const PayloadVmos& UseInputVmos(size_t input_index = 0) const = 0;

  // Returns the |PayloadVmoProvision| for the specified input. This method is
  // only useable if |ConfigureInputToProvideVmos| has been called to
  // configure the specified input, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  virtual PayloadVmoProvision& ProvideInputVmos(size_t input_index = 0) = 0;

  // Requests an input packet on the specified input. |input_index| must be
  // less than the configured input count. This method may be called from
  // |AsyncNode::PutInputPacket|.
  //
  // This method may be called on an arbitrary thread.
  virtual void RequestInputPacket(size_t input_index = 0) = 0;

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to outputs (outbound packets to downstream).
  //////////////////////////////////////////////////////////////////////////////

  // Indicates that the specified output exists but explicityly defers its
  // configuration until a later time. This call is provided so the stage is
  // informed that the output exists, even though the node doesn't know enough
  // at that point to configure the output completely. This allows the output
  // to be connected up by whoever is building the graph. The connection won't
  // transition to ready state (see |OutputConnectionReady|) until the output
  // is fully configured.
  //
  // This method must be called on the main graph thread.
  virtual void ConfigureOutputDeferred(size_t output_index = 0) = 0;

  // Configures an output to address payloads as contiguous regions of process
  // virtual memory allocated by another party. |max_aggregate_payload_size|
  // sets expectations about how much memory will be required for the payloads
  // the output will keep in memory and for the payloads queued on the
  // connection. This value does not include memory required by the connected
  // input. Likewise, |max_payload_count| sets expectations about how many
  // payloads the output will keep in memory at one time plus the number of
  // payloads that may be queued on the connection. |max_payload_size| indicates
  // how large a single payload may be. Either or both of
  // |max_aggregate_payload_size| and |max_payload_count| must be non-zero.
  //
  // Calling this function prohibits the use of |UseOutputVmos| or
  // |ProvideOutputVmos| for the specified output. |AllocatePayloadBuffer|
  // is available for allocating payloads.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::OutputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureOutputToUseLocalMemory(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
      uint64_t max_payload_size, size_t output_index = 0) = 0;

  // Configures an output to allocate its own payloads from local memory. It is
  // assumed that the output can allocate as much memory as is required.
  // TODO(dalesat): Consider committing to handle shortfalls by copying.
  //
  // Calling this function prohibits the use of |UseOutputVmos|,
  // |ProvideOutputVmos| or |AllocatePayloadBuffer| for the specified output.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::OutputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureOutputToProvideLocalMemory(size_t output_index = 0) = 0;

  // Configures an output to address payloads as contiguous regions in VMOs
  // that are created by some other party. |max_aggregate_payload_size|
  // sets expectations about how much memory will be required for the payloads
  // the output will keep in memory and for the payloads queued on the
  // connection. This value does not include memory required by the connected
  // input. Likewise, |max_payload_count| sets expectations about how many
  // payloads the output will keep in memory at one time plus the number of
  // payloads that may be queued on the connection. |max_payload_size| indicates
  // how large a single payload may be. Either |max_aggregate_payload_size| or
  // |max_payload_count| must be non-zero.
  //
  // |vmo_allocation| indicates how the payload buffers must be distributed
  // across the VMOs. |physically_contiguous| indicates whether the VMOs must
  // be physically contiguous. If and only if |physically_contiguous| is true,
  // |bti_handle| provides the handle required for |zx_vmo_create_contiguous|.
  //
  // Calling this function prohibits the use of |ProvideOutputVmos| for the
  // specified output. |UseOutputVmos| is available to determine what VMOs are
  // being used, and |AllocatePayloadBuffer| is available for allocating
  // payloads.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::OutputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureOutputToUseVmos(uint64_t max_aggregate_payload_size,
                                        uint32_t max_payload_count,
                                        uint64_t max_payload_size,
                                        VmoAllocation vmo_allocation,
                                        bool physically_contiguous = false,
                                        zx::handle bti_handle = zx::handle(),
                                        size_t output_index = 0) = 0;

  // Configures an output to address payloads as contiguous regions in VMOs
  // that the output provides. If the VMOs provided by the output are
  // inadequate to hold all the payloads that are kept in memory at one time,
  // the connection will adapt accordingly by creating a separate allocator
  // for the output and doing copies. |vmo_allocation| indicates how the
  // payload buffers will be distributed across the VMOs.
  // |physically_contiguous| indicates whether the VMOs will be contiguous in
  // physical memory.
  //
  // Calling this function allows the use of |ProvideOutputVmos| for the
  // specified output, and |AllocatePayloadBuffer| is available for allocating
  // payloads.
  //
  // Returns true if the connection is ready for allocation activity. Returns
  // false if not, in which case |AsyncNode::OutputConnectionReady| is called
  // when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  virtual bool ConfigureOutputToProvideVmos(VmoAllocation vmo_allocation,
                                            bool physically_contiguous = false,
                                            size_t output_index = 0) = 0;

  // Returns true if the specified input is ready for calls to
  // |AllocatePayloadBuffer|, |UseOutputVmos| or |ProvideOutputVmos|.
  //
  // This method may be called on an arbitrary thread.
  virtual bool OutputConnectionReady(size_t output_index = 0) const = 0;

  // Allocates a payload buffer for the specified output. This method
  // is only useable if a ConfigureOutputFor* method other than
  // |ConfigureOutputToProvideLocalMemory| has been called to configure the
  // specified output, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  virtual fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(
      uint64_t size, size_t output_index = 0) = 0;

  // Returns the |PayloadVmos| for the specified output. This method is only
  // useable if |ConfigureOutputToUseVmos| or |ConfigureOutputToProvideVmos|
  // has been called to configure the specified output, and the connection is
  // ready.
  //
  // This method may be called on an arbitrary thread.
  virtual const PayloadVmos& UseOutputVmos(size_t output_index = 0) const = 0;

  // Returns the |PayloadVmoProvision| for the specified output. This
  // method is only useable if |ConfigureOutputToProvideVmos| has been called
  // to configure the specified output, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  virtual PayloadVmoProvision& ProvideOutputVmos(size_t output_index = 0) = 0;

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

  // Configures inputs and outputs for the node. When this method is called,
  // the node calls ConfigureInputXxx/ConfigureOutputXxx methods on the stage
  // for each input and output the node will support.
  //
  // This method will be called on the graph's thread.
  //
  // TODO(dalesat): Support dynamic reconfiguration.
  virtual void ConfigureConnectors() = 0;

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to inputs (inbound packets from upstream).
  //////////////////////////////////////////////////////////////////////////////

  // Notifies that the connection for the specified input is ready for
  // allocation activity. Note that this method is not called if the connection
  // becomes ready as the result of a call to a ConfigureInputXxx method on
  // the stage. In that case, the ConfigureInputXxx method returns true to
  // indicate the connection is ready.
  virtual void OnInputConnectionReady(size_t input_index) {}

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

  // Notifies that the connection for the specified output is ready for
  // allocation activity. Note that this method is not called if the connection
  // becomes ready as the result of a call to a ConfigureOutputXxx method on
  // the stage. In that case, the ConfigureOutputXxx method returns true to
  // indicate the connection is ready.
  virtual void OnOutputConnectionReady(size_t output_index) {}

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

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_MODELS_ASYNC_NODE_H_
