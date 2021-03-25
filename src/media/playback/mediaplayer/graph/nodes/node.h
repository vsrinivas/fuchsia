// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_NODES_NODE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_NODES_NODE_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/macros.h>

#include <deque>
#include <queue>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/graph/nodes/input.h"
#include "src/media/playback/mediaplayer/graph/nodes/output.h"
#include "src/media/playback/mediaplayer/graph/packet.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_config.h"
#include "src/media/playback/mediaplayer/graph/refs.h"

namespace media_player {

// TODO(dalesat): Ensure that we contractually have all the configuration
// info we need.
// TODO(dalesat): Track payload allocations and complain when usage exceeds
// expectations set by payload configurations.
// TODO(dalesat): Be more precise about the language around the semantics of
// payload configurations.

class Node : public std::enable_shared_from_this<Node> {
 public:
  using AllocateCallback = fit::function<fbl::RefPtr<PayloadBuffer>(uint64_t, const PayloadVmos&)>;

  Node();

  virtual ~Node();

  // Returns a diagnostic label for the node.
  virtual const char* label() const;

  // Generates a report for the node.
  virtual void Dump(std::ostream& os) const;

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
  // allocation activity.
  //
  // This method is called on the graph's thread.
  virtual void OnInputConnectionReady(size_t input_index) {}

  // Notifies that the specified input has a new (replacement) sysmem token.
  //
  // This method is called on the graph's thread.
  virtual void OnNewInputSysmemToken(size_t input_index) {}

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
  virtual void FlushInput(bool hold_frame, size_t input_index, fit::closure callback) {
    FX_CHECK(false) << "FlushInput not implemented.";
  }

  // Supplies the node with a packet that arrived on the specified input. This
  // method may call |NodeStage::RequestInputPacket|. The default
  // implementation aborts.
  //
  // This method will be called on the graph's thread.
  virtual void PutInputPacket(PacketPtr packet, size_t input_index) {
    FX_CHECK(false) << "PutInputPacket not implemented.";
  }

  // Returns the number of input connections.
  size_t input_count() const;

  // Returns the indicated input connection.
  Input& input(size_t index);

  // Returns the number of output connections.
  size_t output_count() const;

  // Returns the indicated output connection.
  Output& output(size_t index);

  // Notifies the node that the connection for the indicated input is ready
  // for allocation activity.
  //
  // This method may be called on an arbitrary thread.
  void NotifyInputConnectionReady(size_t index);

  // Notifies the node that the connection for the indicated output is ready
  // for allocation activity.
  //
  // This method may be called on an arbitrary thread.
  void NotifyOutputConnectionReady(size_t index);

  // Notifies the node that the connection for the indicated input has a new (replacement) sysmem
  // token.
  //
  // This method may be called on an arbitrary thread.
  void NotifyNewInputSysmemToken(size_t index);

  // Notifies the node that the connection for the indicated output has a new (replacement) sysmem
  // token.
  //
  // This method may be called on an arbitrary thread.
  void NotifyNewOutputSysmemToken(size_t index);

  // Flushes an input. |hold_frame| indicates whether a video renderer should
  // hold and display the newest frame. The callback is used to indicate that
  // the flush operation is complete. It must be called on the graph's thread
  // and may be called synchronously.
  //
  // The input in question must be flushed (|Input.Flush|) synchronously with
  // this call to eject the queued packet (if there is one) and clear the
  // input's need for a packet. The callback is provided in case the node
  // has additional flushing business that can't be completed synchronously.
  void FlushInputExternal(size_t index, bool hold_frame, fit::closure callback);

  // Flushes an output. The callback is used to indicate that the flush
  // operation is complete. It must be called on the graph's thread and may be
  // called synchronously. The callback is provided in case the node has
  // additional flushing business that can't be completed synchronously.
  //
  // The output in question must not produce any packets after this method is
  // called and before the need for a packet is signalled.
  void FlushOutputExternal(size_t index, fit::closure callback);

  // Shuts down the stage.
  void ShutDown();

  // Queues the stage for update if it isn't already queued. This method may
  // be called on any thread.
  void NeedsUpdate();

  // Calls |Update| until no more updates are required.
  void UpdateUntilDone();

  // Acquires the stage, preventing posted tasks from running until the stage
  // is released. |callback| is called when the stage is acquired.
  void Acquire(fit::closure callback);

  // Releases the stage previously acquired via |Acquire|.
  void Release();

  // Sets an |async_t| for running tasks .
  void SetDispatcher(async_dispatcher_t* dispatcher);

  //////////////////////////////////////////////////////////////////////////////
  // Methods relating to outputs (outbound packets to downstream).
  //////////////////////////////////////////////////////////////////////////////

  // Notifies that the connection for the specified output is ready for
  // allocation activity.
  //
  // This method is called on the graph's thread.
  virtual void OnOutputConnectionReady(size_t output_index) {}

  // Notifies that the specified output has a new (replacement) sysmem token.
  //
  // This method is called on the graph's thread.
  virtual void OnNewOutputSysmemToken(size_t output_index) {}

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
    FX_CHECK(false) << "FlushOutput not implemented.";
  }

  // Requests an output packet. The default implementation aborts.
  //
  // This method will be called on the graph's thread.
  virtual void RequestOutputPacket() { FX_CHECK(false) << "RequestOutputPacket not implemented."; }

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
  void ConfigureInputDeferred(size_t input_index = 0);

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
  // |Node::OnInputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureInputToUseLocalMemory(uint64_t max_aggregate_payload_size,
                                      uint32_t max_payload_count,
                                      zx_vm_option_t map_flags = ZX_VM_PERM_READ,
                                      size_t input_index = 0);

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
  // across the VMOs.
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
  // |Node::OnInputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|).
  // Otherwise, it must be called on the main graph thread.
  void ConfigureInputToUseVmos(uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
                               uint64_t max_payload_size, VmoAllocation vmo_allocation,
                               zx_vm_option_t map_flags = ZX_VM_PERM_READ,
                               AllocateCallback allocate_callback = nullptr,
                               size_t input_index = 0);

  // Configures an input to address payloads as contiguous regions in VMOs
  // that the input provides. If the VMOs provided by the input are
  // inadequate to hold all the payloads that are kept in memory at one time,
  // the connection will adapt accordingly by creating a separate allocator
  // for the output and doing copies. |vmo_allocation| indicates how the
  // payload buffers will be distributed across the VMOs.
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
  // |Node::OnInputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the input has been
  // configured previously (possibly with |ConfigureInputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureInputToProvideVmos(VmoAllocation vmo_allocation,
                                   zx_vm_option_t map_flags = ZX_VM_PERM_READ,
                                   AllocateCallback allocate_callback = nullptr,
                                   size_t input_index = 0);

  // Configures an input to address payloads as contiguous regions in VMOs provided by sysmem. If
  // the VMOs provided by sysmem are inadequate to hold all the payloads that are kept in memory
  // at one time, the connection will adapt accordingly by creating a separate allocator for the
  // output and doing copies. |vmo_allocation| indicates how the payload buffers will be distributed
  // across the VMOs.
  //
  // Calling this function allows the use of |TakeInputSysmemToken| for the specified input.
  //
  // |Node::OnInputConnectionReady| is called when the connection becomes ready.
  //
  // This method must be called on the main graph thread.
  void ConfigureInputToUseSysmemVmos(ServiceProvider* service_provider,
                                     uint64_t max_aggregate_payload_size,
                                     uint32_t max_payload_count, uint64_t max_payload_size,
                                     VmoAllocation vmo_allocation,
                                     zx_vm_option_t map_flags = ZX_VM_PERM_READ,
                                     AllocateCallback allocate_callback = nullptr,
                                     size_t input_index = 0);

  // Returns true if the specified output is ready for calls to |UseInputVmos|
  // or |ProvideInputVmos|.
  //
  // This method may be called on an arbitrary thread.
  bool InputConnectionReady(size_t input_index = 0) const;

  // Returns the |PayloadVmos| for the specified input. This method is only
  // is only useable if |ConfigureInputToUseVmos| or
  // |ConfigureInputToProvideVmos| has been called to configure the specified
  // input, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  const PayloadVmos& UseInputVmos(size_t input_index = 0) const;

  // Returns the |PayloadVmoProvision| for the specified input. This method is
  // only useable if |ConfigureInputToProvideVmos| has been called to
  // configure the specified input, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  PayloadVmoProvision& ProvideInputVmos(size_t input_index = 0) const;

  // Takes the sysmem buffer collection token for the specified input. This method is only usable
  // if |ConfigureInputToUseSysmemVmos| has been called to configure the specified input.
  //
  // This method may be called on an arbitrary thread.
  fuchsia::sysmem::BufferCollectionTokenPtr TakeInputSysmemToken(size_t input_index = 0);

  // Requests an input packet on the specified input. |input_index| must be
  // less than the configured input count. This method may be called from
  // |Node::PutInputPacket|.
  //
  // This method may be called on an arbitrary thread.
  void RequestInputPacket(size_t input_index = 0);

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
  void ConfigureOutputDeferred(size_t output_index = 0);

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
  // |video_constraints| must be provided for uncompressed video outputs in the event that the
  // connected input is configured using |ConfigureInputToUseSysmemVmos|, in which case the
  // constraints are needed to constrain the sysmem buffer collection properly.
  //
  // |Node::OnOutputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureOutputToUseLocalMemory(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count, uint64_t max_payload_size,
      zx_vm_option_t map_flags = ZX_VM_PERM_WRITE,
      std::shared_ptr<fuchsia::sysmem::ImageFormatConstraints> video_constraints = nullptr,
      size_t output_index = 0);

  // Configures an output to allocate its own payloads from local memory. It is
  // assumed that the output can allocate as much memory as is required. The size and count values
  // aren't used unless packet payloads need to be copied into a limited collection of payload
  // buffers.
  // TODO(dalesat): Consider committing to handle shortfalls by copying.
  //
  // Calling this function prohibits the use of |UseOutputVmos|,
  // |ProvideOutputVmos| or |AllocatePayloadBuffer| for the specified output.
  //
  // |video_constraints| must be provided for uncompressed video outputs in the event that the
  // connected input is configured using |ConfigureInputToUseSysmemVmos|, in which case the
  // constraints are needed to constrain the sysmem buffer collection properly.
  //
  // |Node::OnOutputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureOutputToProvideLocalMemory(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count, uint64_t max_payload_size,
      std::shared_ptr<fuchsia::sysmem::ImageFormatConstraints> video_constraints = nullptr,
      size_t output_index = 0);

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
  // across the VMOs.
  //
  // Calling this function prohibits the use of |ProvideOutputVmos| for the
  // specified output. |UseOutputVmos| is available to determine what VMOs are
  // being used, and |AllocatePayloadBuffer| is available for allocating
  // payloads.
  //
  // |video_constraints| must be provided for uncompressed video outputs in the event that the
  // connected input is configured using |ConfigureInputToUseSysmemVmos|, in which case the
  // constraints are needed to constrain the sysmem buffer collection properly.
  //
  // |Node::OnOutputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureOutputToUseVmos(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count, uint64_t max_payload_size,
      VmoAllocation vmo_allocation, zx_vm_option_t map_flags = ZX_VM_PERM_WRITE,
      std::shared_ptr<fuchsia::sysmem::ImageFormatConstraints> video_constraints = nullptr,
      size_t output_index = 0);

  // Configures an output to address payloads as contiguous regions in VMOs
  // that the output provides. If the VMOs provided by the output are
  // inadequate to hold all the payloads that are kept in memory at one time,
  // the connection will adapt accordingly by creating a separate allocator
  // for the output and doing copies. |vmo_allocation| indicates how the
  // payload buffers will be distributed across the VMOs.
  //
  // Calling this function allows the use of |ProvideOutputVmos| for the
  // specified output, and |AllocatePayloadBuffer| is available for allocating
  // payloads.
  //
  // |video_constraints| must be provided for uncompressed video outputs in the event that the
  // connected input is configured using |ConfigureInputToUseSysmemVmos|, in which case the
  // constraints are needed to constrain the sysmem buffer collection properly.
  //
  // |Node::OnOutputConnectionReady| is called when the connection becomes ready.
  //
  // This method may be called on any thread provided the output has been
  // configured previously (possibly with |ConfigureOutputDeferred|). Otherwise,
  // it must be called on the main graph thread.
  void ConfigureOutputToProvideVmos(
      VmoAllocation vmo_allocation, zx_vm_option_t map_flags = ZX_VM_PERM_WRITE,
      std::shared_ptr<fuchsia::sysmem::ImageFormatConstraints> video_constraints = nullptr,
      size_t output_index = 0);

  // Configures an output to address payloads as contiguous regions in VMOs provided by sysmem. If
  // the VMOs provided by sysmem are inadequate to hold all the payloads that are kept in memory
  // at one time, the connection will adapt accordingly by creating a separate allocator for the
  // input and doing copies. |vmo_allocation| indicates how the payload buffers will be distributed
  // across the VMOs.
  //
  // |video_constraints| must be provided for uncompressed video outputs in the event that the
  // connected input is configured using |ConfigureInputToUseSysmemVmos| and the input configration
  // is incompatible, in which case the constraints are needed to constrain the sysmem buffer
  // collection properly.
  //
  // Calling this function allows the use of |TakeOutputSysmemToken| for the specified output.
  //
  // |Node::OnOutputConnectionReady| is called when the connection becomes ready.
  //
  // This method must be called on the main graph thread.
  void ConfigureOutputToUseSysmemVmos(
      ServiceProvider* service_provider, uint64_t max_aggregate_payload_size,
      uint32_t max_payload_count, uint64_t max_payload_size, VmoAllocation vmo_allocation,
      zx_vm_option_t map_flags = ZX_VM_PERM_WRITE,
      std::shared_ptr<fuchsia::sysmem::ImageFormatConstraints> video_constraints = nullptr,
      size_t output_index = 0);

  // Returns true if the specified input is ready for calls to
  // |AllocatePayloadBuffer|, |UseOutputVmos| or |ProvideOutputVmos|.
  //
  // This method may be called on an arbitrary thread.
  bool OutputConnectionReady(size_t output_index = 0) const;

  // Allocates a payload buffer for the specified output. This method
  // is only useable if a ConfigureOutputFor* method other than
  // |ConfigureOutputToProvideLocalMemory| has been called to configure the
  // specified output, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(uint64_t size, size_t output_index = 0);

  // Returns the |PayloadVmos| for the specified output. This method is only
  // useable if |ConfigureOutputToUseVmos| or |ConfigureOutputToProvideVmos|
  // has been called to configure the specified output, and the connection is
  // ready.
  //
  // This method may be called on an arbitrary thread.
  const PayloadVmos& UseOutputVmos(size_t output_index = 0) const;

  // Returns the |PayloadVmoProvision| for the specified output. This
  // method is only useable if |ConfigureOutputToProvideVmos| has been called
  // to configure the specified output, and the connection is ready.
  //
  // This method may be called on an arbitrary thread.
  PayloadVmoProvision& ProvideOutputVmos(size_t output_index = 0) const;

  // Takes the sysmem buffer collection token for the specified output. This method is only usable
  // if |ConfigureOutputToUseSysmemVmos| has been called to configure the specified output.
  //
  // This method may be called on an arbitrary thread.
  fuchsia::sysmem::BufferCollectionTokenPtr TakeOutputSysmemToken(size_t output_index = 0);

  // Supplies a packet to be sent downstream on the specified output.
  //
  // This method may be called on an arbitrary thread.
  void PutOutputPacket(PacketPtr packet, size_t output_index = 0);

 protected:
  // Posts a task to run as soon as possible. A task posted with this method is
  // run exclusive of any other such tasks.
  void PostTask(fit::closure task);

  // Updates packet supply and demand.
  void Update();

  // Post a task that will run even if the stage has been shut down.
  void PostShutdownTask(fit::closure task);

 private:
  // Runs tasks in the task queue. This method is always called from
  // |dispatcher_|. A |StageImpl| funnels all task execution through
  // |RunTasks|. The lambdas that call |RunTasks| capture a shared pointer to
  // the stage, so the stage can't be deleted from the time such a lambda is
  // created until it's done executing |RunTasks|. A stage that's no longer
  // referenced by the graph will be deleted when all such lambdas have
  // completed. |ShutDown| prevents |RunTasks| from actually executing any
  // tasks.
  void RunTasks();

  // Takes a packet from the queue for |output| if that queue isn't empty and
  // the output needs a packet. Returns true if and only if the queue is empty
  // and the output needs a packet.
  bool MaybeTakePacketForOutput(const Output& output, PacketPtr* packet_out);

  void DumpInputDetail(std::ostream& os, const Input& input) const;

  void DumpOutputDetail(std::ostream& os, const Output& output) const;

  void EnsureInput(size_t input_index);

  void EnsureOutput(size_t output_index);

  void ApplyInputConfiguration(Input* input,
                               PayloadManager::AllocateCallback allocate_callback = nullptr,
                               ServiceProvider* service_provider = nullptr);

  void ApplyOutputConfiguration(Output* output, ServiceProvider* service_provider = nullptr);

  // The stage's thread is always the main graph thread.
  FIT_DECLARE_THREAD_CHECKER(thread_checker_);

  async_dispatcher_t* dispatcher_;

  // Used for ensuring the stage is properly updated. This value is zero
  // initially, indicating that there's no need to update the stage. When the
  // stage needs updating, the counter is incremented. A transition from 0 to
  // 1 indicates that the stage should be enqueued. Before the update occurs,
  // this value is set to 1. If it's no longer 1 after update completes, it is
  // updated again. When an update completes and the counter is still 1, the
  // counter is reset to 0.
  std::atomic_uint32_t update_counter_;

  mutable std::mutex tasks_mutex_;
  // Pending tasks. Only |RunTasks| may pop from this queue.
  std::queue<fit::closure> tasks_ FXL_GUARDED_BY(tasks_mutex_);
  // Set to true to suspend task execution.
  bool tasks_suspended_ FXL_GUARDED_BY(tasks_mutex_) = false;

  // These fields are modified on the main graph thread only.
  std::vector<Input> inputs_;
  std::vector<Output> outputs_;

  mutable std::mutex packets_per_output_mutex_;
  std::vector<std::deque<PacketPtr>> packets_per_output_ FXL_GUARDED_BY(packets_per_output_mutex_);

  friend class Graph;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_NODES_NODE_H_
