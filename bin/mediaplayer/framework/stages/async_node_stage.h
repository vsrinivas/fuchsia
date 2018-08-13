// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_ASYNC_NODE_STAGE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_ASYNC_NODE_STAGE_H_

#include <deque>
#include <mutex>

#include "garnet/bin/mediaplayer/framework/models/async_node.h"
#include "garnet/bin/mediaplayer/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

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

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t input_index) override;

  void PrepareOutput(size_t output_index,
                     std::shared_ptr<PayloadAllocator> allocator) override;

  void UnprepareOutput(size_t output_index) override;

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

  void RequestInputPacket(size_t input_index = 0) override;

  void PutOutputPacket(PacketPtr packet, size_t output_index = 0) override;

  // Takes a packet from the queue for |output| if that queue isn't empty and
  // the output needs a packet. Returns true if and only if the queue is empty
  // and the output needs a packet.
  bool MaybeTakePacketForOutput(const Output& output, PacketPtr* packet_out);

  void DumpInputDetail(std::ostream& os, const Input& input) const;

  void DumpOutputDetail(std::ostream& os, const Output& output) const;

  // The fields below are not changed between the completion of the constructor
  // and the initiation of the destructor.
  std::shared_ptr<AsyncNode> node_;
  std::vector<Input> inputs_;
  std::vector<Output> outputs_;

  mutable std::mutex packets_per_output_mutex_;
  std::vector<std::deque<PacketPtr>> packets_per_output_
      FXL_GUARDED_BY(packets_per_output_mutex_);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_ASYNC_NODE_STAGE_H_
