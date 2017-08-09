// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>

#include "apps/media/lib/flog/flog.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/lib/transport/shared_buffer_set.h"
#include "apps/media/services/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/services/media_transport.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/synchronization/thread_checker.h"
#include "lib/ftl/tasks/task_runner.h"

namespace media {

// Base class that implements MediaPacketConsumer.
class MediaPacketConsumerBase : public MediaPacketConsumer {
 private:
  class SuppliedPacketCounter;

 public:
  MediaPacketConsumerBase();

  ~MediaPacketConsumerBase() override;

  // Wraps a supplied MediaPacket and calls the associated callback when
  // destroyed. Also works with SuppliedPacketCounter to keep track of the
  // number of outstanding packets and to deliver demand updates.
  class SuppliedPacket {
   public:
    ~SuppliedPacket();

    const MediaPacketPtr& packet() { return packet_; }
    void* payload() { return payload_; }
    uint64_t payload_size() { return packet_->payload_size; }
    uint64_t label() { return label_; }

   private:
    SuppliedPacket(uint64_t label,
                   MediaPacketPtr packet,
                   void* payload,
                   const SupplyPacketCallback& callback,
                   std::shared_ptr<SuppliedPacketCounter> counter);

    uint64_t label_;
    MediaPacketPtr packet_;
    void* payload_;
    SupplyPacketCallback callback_;
    std::shared_ptr<SuppliedPacketCounter> counter_;

    FTL_DECLARE_THREAD_CHECKER(thread_checker_);

    // So the constructor can be private.
    friend class MediaPacketConsumerBase;
  };

  // Binds to this MediaPacketConsumer.
  void Bind(fidl::InterfaceRequest<MediaPacketConsumer> request);

  // Binds to this MediaPacketConsumer.
  void Bind(fidl::InterfaceHandle<MediaPacketConsumer>* handle);

  // Determines if the consumer is bound to a channel.
  bool is_bound();

  // Sets the PTS rate to apply to all incoming packets. If the PTS rate is
  // set to TimelineRate::Zero (the default), PTS rates on incoming packets
  // are not adjusted.
  void SetPtsRate(TimelineRate pts_rate) { pts_rate_ = pts_rate; }

  // Indicates that revised media type is to be accepted.
  void AcceptRevisedMediaType() { accept_revised_media_type_ = true; }

  const MediaPacketDemand& current_demand() { return demand_; }

  // Sets the demand, which is communicated back to the producer at the first
  // opportunity (in response to PullDemandUpdate or SupplyPacket).
  void SetDemand(uint32_t min_packets_outstanding,
                 int64_t min_pts = MediaPacket::kNoTimestamp);

  // Shuts down the consumer.
  void Reset();

  // Shuts down the consumer and calls OnFailure().
  void Fail();

 protected:
  // Called when a packet is supplied.
  virtual void OnPacketSupplied(
      std::unique_ptr<SuppliedPacket> supplied_packet) = 0;

  // Called upon the return of a supplied packet after the value returned by
  // supplied_packets_outstanding() has been updated and before the callback is
  // called. This is often a good time to call SetDemand. The default
  // implementation does nothing.
  virtual void OnPacketReturning();

  // Called when the consumer is asked to flush. The default implementation
  // just runs the callback.
  virtual void OnFlushRequested(bool hold_frame, const FlushCallback& callback);

  // Called when the binding is unbound. The default implementation does
  // nothing. Subclasses may delete themselves in overrides of |OnUnbind|.
  virtual void OnUnbind();

  // Called when a fatal error occurs. The default implementation does nothing.
  virtual void OnFailure();

  uint32_t supplied_packets_outstanding() {
    return counter_->packets_outstanding();
  }

 private:
  // MediaPacketConsumer implementation.
  void PullDemandUpdate(const PullDemandUpdateCallback& callback) final;

  void AddPayloadBuffer(uint32_t payload_buffer_id,
                        mx::vmo payload_buffer) final;

  void RemovePayloadBuffer(uint32_t payload_buffer_id) final;

  void SupplyPacket(MediaPacketPtr packet,
                    const SupplyPacketCallback& callback) final;

  void Flush(bool hold_frame, const FlushCallback& callback) final;

  // Counts oustanding supplied packets and uses their callbacks to deliver
  // demand updates. This class is referenced using shared_ptrs so that no
  // SuppliedPackets outlive it.
  // TODO(dalesat): Get rid of this separate class by insisting that the
  // MediaPacketConsumerBase outlive its SuppliedPackets.
  class SuppliedPacketCounter {
   public:
    SuppliedPacketCounter(MediaPacketConsumerBase* owner);

    ~SuppliedPacketCounter();

    ftl::RefPtr<ftl::TaskRunner>& task_runner() { return task_runner_; }

    // Prevents any subsequent calls to the owner.
    void Detach() {
      FTL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
      owner_ = nullptr;
    }

    // Records the arrival of a packet.
    void OnPacketArrival() {
      FTL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
      ++packets_outstanding_;
    }

    // Records the departure of a packet and returns the current demand update,
    // if any.
    MediaPacketDemandPtr OnPacketDeparture(uint64_t label) {
      FTL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
      --packets_outstanding_;
      return (owner_ == nullptr) ? nullptr
                                 : owner_->GetDemandForPacketDeparture(label);
    }

    // Returns number of packets currently outstanding.
    uint32_t packets_outstanding() { return packets_outstanding_; }

    SharedBufferSet& buffer_set() { return buffer_set_; }

   private:
    MediaPacketConsumerBase* owner_;
    // We keep the buffer set here, because it needs to outlive SuppliedPackets.
    ftl::RefPtr<ftl::TaskRunner> task_runner_;
    SharedBufferSet buffer_set_;
    std::atomic_uint32_t packets_outstanding_;

    FTL_DECLARE_THREAD_CHECKER(thread_checker_);
  };

  // Completes a pending PullDemandUpdate if there is one and if there's an
  // update to send.
  void MaybeCompletePullDemandUpdate();

  // Returns the demand update, if any, to be included in a SupplyPacket
  // callback.
  MediaPacketDemandPtr GetDemandForPacketDeparture(uint64_t label);

  // Sets the PTS rate of the packet to pts_rate_ unless pts_rate_ is zero.
  // Does nothing if pts_rate_ is zero.
  void SetPacketPtsRate(const MediaPacketPtr& packet);

  fidl::Binding<MediaPacketConsumer> binding_;
  bool accept_revised_media_type_ = false;
  MediaPacketDemand demand_;
  bool demand_update_required_ = false;
  bool returning_packet_ = false;
  PullDemandUpdateCallback get_demand_update_callback_;
  std::shared_ptr<SuppliedPacketCounter> counter_;
  TimelineRate pts_rate_ = TimelineRate::Zero;  // Zero means do not adjust.
  uint64_t prev_packet_label_ = 0;
  bool flush_pending_ = false;
  bool is_reset_ = true;

  FTL_DECLARE_THREAD_CHECKER(thread_checker_);

  FLOG_INSTANCE_CHANNEL(logs::MediaPacketConsumerChannel, log_channel_);
};

}  // namespace media
