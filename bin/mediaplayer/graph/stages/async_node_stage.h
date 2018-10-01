// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_STAGES_ASYNC_NODE_STAGE_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_STAGES_ASYNC_NODE_STAGE_H_

#include <deque>
#include <mutex>

#include "garnet/bin/mediaplayer/graph/models/async_node.h"
#include "garnet/bin/mediaplayer/graph/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace media_player {

// A stage that hosts an AsyncNode.
class AsyncNodeStageImpl : public AsyncNodeStage, public StageImpl {
 public:
  AsyncNodeStageImpl(std::shared_ptr<AsyncNode> node);

  ~AsyncNodeStageImpl() override;

  // StageImpl implementation.
  void OnShutDown() override;

  size_t input_count() const override;

  Input& input(size_t input_index) override;

  size_t output_count() const override;

  Output& output(size_t output_index) override;

  void NotifyInputConnectionReady(size_t index) override;

  void NotifyOutputConnectionReady(size_t index) override;

  void FlushInput(size_t input_index, bool hold_frame,
                  fit::closure callback) override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() const override;

  void Update() override;

 private:
  // AsyncNodeStage implementation.
  void PostTask(fit::closure task) override;

  void Dump(std::ostream& os) const override;

  void ConfigureInputDeferred(size_t input_index = 0) override;

  bool ConfigureInputToUseLocalMemory(uint64_t max_aggregate_payload_size,
                                      uint32_t max_payload_count,
                                      size_t input_index = 0) override;

  bool ConfigureInputToUseVmos(
      uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
      uint64_t max_payload_size, VmoAllocation vmo_allocation,
      bool physically_contiguous, zx::handle bti_handle,
      AllocateCallback allocate_callback, size_t input_index = 0) override;

  bool ConfigureInputToProvideVmos(VmoAllocation vmo_allocation,
                                   bool physically_contiguous,
                                   AllocateCallback allocate_callback,
                                   size_t input_index = 0) override;

  bool InputConnectionReady(size_t input_index = 0) const override;

  const PayloadVmos& UseInputVmos(size_t input_index = 0) const override;

  PayloadVmoProvision& ProvideInputVmos(size_t input_index = 0) override;

  void RequestInputPacket(size_t input_index = 0) override;

  void ConfigureOutputDeferred(size_t output_index = 0) override;

  bool ConfigureOutputToUseLocalMemory(uint64_t max_aggregate_payload_size,
                                       uint32_t max_payload_count,
                                       uint64_t max_payload_size,
                                       size_t output_index = 0) override;

  bool ConfigureOutputToProvideLocalMemory(size_t output_index = 0) override;

  bool ConfigureOutputToUseVmos(uint64_t max_aggregate_payload_size,
                                uint32_t max_payload_count,
                                uint64_t max_payload_size,
                                VmoAllocation vmo_allocation,
                                bool physically_contiguous,
                                zx::handle bti_handle,
                                size_t output_index = 0) override;

  bool ConfigureOutputToProvideVmos(VmoAllocation vmo_allocation,
                                    bool physically_contiguous,
                                    size_t output_index = 0) override;

  bool OutputConnectionReady(size_t output_index = 0) const override;

  fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(
      uint64_t size, size_t output_index = 0) override;

  const PayloadVmos& UseOutputVmos(size_t output_index = 0) const override;

  PayloadVmoProvision& ProvideOutputVmos(size_t output_index = 0) override;

  void PutOutputPacket(PacketPtr packet, size_t output_index = 0) override;

  // Takes a packet from the queue for |output| if that queue isn't empty and
  // the output needs a packet. Returns true if and only if the queue is empty
  // and the output needs a packet.
  bool MaybeTakePacketForOutput(const Output& output, PacketPtr* packet_out);

  void DumpInputDetail(std::ostream& os, const Input& input) const;

  void DumpOutputDetail(std::ostream& os, const Output& output) const;

  void EnsureInput(size_t input_index);

  void EnsureOutput(size_t output_index);

  // The stage's thread is always the main graph thread.
  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  // This field is set in the constructor and not modified thereafter.
  std::shared_ptr<AsyncNode> node_;

  // These fields are modified on the main graph thread only.
  std::vector<Input> inputs_;
  std::vector<Output> outputs_;

  mutable std::mutex packets_per_output_mutex_;
  std::vector<std::deque<PacketPtr>> packets_per_output_
      FXL_GUARDED_BY(packets_per_output_mutex_);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_STAGES_ASYNC_NODE_STAGE_H_
