// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_

#include <mutex>
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/local_memory_payload_allocator.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_config.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/vmo_payload_allocator.h"

namespace media_player {

// DESIGN
//
// |PayloadManager| manages payload allocation for a connection. Its
// responsibilities are:
//
// 1) Assemble the right configuration of allocators based on the
//    |PayloadConfig|s from the output and input.
// 2) Initialize the allocators prior to use by the output and input.
// 3) Expose the right capabilities to the output and input.
// 4) Arrange for payload copying when needed.
//
// |PayloadConfig| is described in detail in payload_config.h.
//
// The term 'connector' is used to refer to either the output or input.
//
// The allocator configuration may include zero, one or two allocators, and
// and there are two kinds of allocators, |LocalMemoryPayloadAllocator| and
// |VmoPayloadAllocator|. We associate a particular allocator with either the
// output or the input, though in some cases, both parties can access the same
// VMO allocator. In such cases, the allocator is associated with:
// 1) the connector supplying VMOs to the allocator, if there is one, otherwise
// 2) the connector that needs VMO access, if only one does, otherwise
// 3) the input.
// Associating the allocator with the input in the last case is arbitrary, in
// some respects, but it simplifies the code that deals with the input's
// requirement to perform allocations itself. See the |allocate_callback|
// parameter of |ApplyInputConfiguration|.
//
// When copying is performed, payloads produced by the output are copied to
// memory allocated from the input's allocator.
//
// In most cases, the correct allocator configuration can be established when
// both the output and the input have supplied their |PayloadConfig|s. There
// are other cases in which incompatibility is detected when VMOs are provided
// by the input or output, in which case the allocator configuration must be
// changed to have two allocators.

// Manages payload allocation for a connection, selecting and implementing the
// correct allocation strategy based on the constraints expressed by the
// output and input.
//
// |PayloadManager| is thread-safe. All of its methods may be called on any
// thread.
class PayloadManager {
 public:
  // Function type used by clients who want to implement buffer allocation
  // themselves.
  //
  // uint64_t size           - size in bytes of the buffer
  // const PayloadVmos& vmos - the VMO collection from which to allocate
  // (result)                - A |PayloadBuffer| whose size is >= to the
  //                           requested size, or nullptr if the allocation
  //                           failed.
  //
  // The allocator callback is called on an arbitrary thread.
  //
  // The supplied VMOs are the same ones available on the node via
  // |Node::UseOutputVmos| or |Node::UseInputVmos|. They're passed to the
  // callback, because the callback may not call back into the node.
  using AllocateCallback =
      fit::function<fbl::RefPtr<PayloadBuffer>(uint64_t, const PayloadVmos&)>;

  // Dumps this |PayloadManager|'s state to |os|.
  void Dump(std::ostream& os) const;

  // Applies the output configuration supplied in |config|. |bti_handle| must
  // be provided if and only if |config.physically_contiguous| is true.
  void ApplyOutputConfiguration(const PayloadConfig& config,
                                zx::handle bti_handle);

  // Applies the input configuration supplied in |config|.
  //
  // |allocate_callback| may be supplied for VMO modes only. It allows the node
  // to perform the actual allocations against the VMOs. The allocator callback
  // will never be asked to allocate from VMOs provided by the output.
  // |allocate_callback| is called on an arbitrary thread, and may not reenter
  // this |PayloadManager|.
  //
  // |bti_handle| must be provided if and only if |config.physically_contiguous|
  // is true.
  void ApplyInputConfiguration(const PayloadConfig& config,
                               zx::handle bti_handle,
                               AllocateCallback allocate_callback);

  // Indicates whether the connection manager is ready for allocator access.
  bool ready() const;

  // Allocates and returns a |PayloadBuffer| for the output with the specified
  // size. Returns nullptr if the allocation fails.
  fbl::RefPtr<PayloadBuffer> AllocatePayloadBufferForOutput(
      uint64_t size) const;

  // Gets the |PayloadVmos| interface for the input. This method should only be
  // called if this |PayloadManager| is ready and the input mode is |kUsesVmos|
  // or |kProvidesVmos|.
  PayloadVmos& input_vmos() const;

  // Gets the |PayloadVmoProvision| interface for the input. This method should
  // only be called if this |PayloadManager| is ready and the input mode is
  // |kProvidesVmos|.
  PayloadVmoProvision& input_external_vmos() const;

  // Gets the |PayloadVmos| interface for the output. This method should only be
  // called if this |PayloadManager| is ready and the output mode is |kUsesVmos|
  // or |kProvidesVmos|.
  PayloadVmos& output_vmos() const;

  // Gets the |PayloadVmoProvision| interface for the output. This method should
  // only be called if this |PayloadManager| is ready and the output mode is
  // |kProvidesVmos|.
  PayloadVmoProvision& output_external_vmos() const;

  // Indicates whether copying is required and maybe provides a copy destination
  // payload buffer. This method returns true if and only if copying is required
  // for this connection. If copying is required and |size| is non-zero, this
  // method will attempt to allocate a payload buffer into which |size| bytes
  // of payload may be copied. If this method returns true, |size| is non-zero
  // and |*payload_buffer_out| is nullptr after the method returns, this
  // indicates that payload memory for this purpose is exhausted.
  //
  // |payload_buffer_out| may only be nullptr if |size| is also zero.
  bool MaybeAllocatePayloadBufferForCopy(
      uint64_t size, fbl::RefPtr<PayloadBuffer>* payload_buffer_out) const;

  // Signals that the output and input are disconnected.
  void OnDisconnect();

 private:
  // State relating to output or input.
  struct Connector {
    PayloadConfig config_;
    zx::handle bti_handle_;
    fbl::RefPtr<LocalMemoryPayloadAllocator> local_memory_allocator_;
    fbl::RefPtr<VmoPayloadAllocator> vmo_allocator_;

    // Ensure that this |Connector| has no allocators.
    void EnsureNoAllocator();

    // Ensure that this |Connector| has only a local memory allocator.
    void EnsureLocalMemoryAllocator();

    // Ensure that this |Connector| has only a VMO allocator. Returns a raw
    // pointer to the VMO allocator.
    VmoPayloadAllocator* EnsureVmoAllocator();

    // Return a |PayloadAllocator| implemented by this connector, if there is
    // one, nullptr otherwise.
    PayloadAllocator* payload_allocator() const;
  };

  void DumpInternal(std::ostream& os) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Indicates whether the connection manager is ready for allocator access.
  bool ready_locked() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Updates the allocators based in the current configs.
  void UpdateAllocators() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines whether the output and input configuration are compatible.
  // The |mode_| values are not examined and are assumed to be compatible.
  // When |kProvidesVmos| mode is used, incompatibility may not be detected
  // until VMOs are supplied.
  bool ConfigsAreCompatible() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Determines whether the output and input configuration modes are compatible.
  // This method is only used for a DCHECK in |ConfigsAreCompatible|.
  bool ConfigModesAreCompatible() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns a |VmoAllocation| value that satisfies both output and input,
  // either |kSingleVmo| or |kVmoPerBuffer|. The output and input must have
  // compatible |config_.vmo_allocation_| values.
  VmoAllocation CombinedVmoAllocation() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Creates VMOs for an allocator shared by the input and output and adds them
  // to |allocator|. The VMOs created will satisfy the requirements of both the
  // output and the input.
  void ProvideVmosForSharedAllocator(VmoPayloadAllocator* allocator) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Creates VMOs and adds them to |allocator|. The VMOs created will satisfy
  // the specified configuration.
  //
  // This method is used in two cases:
  // 1) The allocator is associated with only the output or the input, in which
  //    case |config| is the configuration for that output or input.
  // 2) When an allocator is shared between the output and input, in which case
  //    |config| is the merged configuration of the output and the input.
  //    |ProvideVmosForSharedAllocator| merges the configurations and calls
  //    this method.
  //
  // The larger of |max_payload_size| and |config.max_payload_size_| will be
  // used. When providing VMOs for an input, |max_payload_size| should be the
  // max payload size from the output's config. Otherwise, it should be zero.
  // |bti_handle| is provided to indicate that the VMOs must be physically
  // contiguous.
  void ProvideVmos(VmoPayloadAllocator* allocator, const PayloadConfig& config,
                   uint64_t max_payload_size,
                   const zx::handle* bti_handle) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Prepares |allocator| for external VMOs by settings its |VmoAllocation|
  // setting based on |config|. This method is used when |allocator| is
  // associated with only the output or the input (not both). |config| is the
  // configuration for that output or input.
  void PrepareForExternalVmos(VmoPayloadAllocator* allocator,
                              const PayloadConfig& config) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Prepares |allocator| for external VMOs by settings its |VmoAllocation|
  // setting based on the requirements of both the output and the input. This
  // method is used when |allocator| is shared by the output and input.
  void PrepareSharedAllocatorForExternalVmos(VmoPayloadAllocator* allocator)
      const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Allocates and returns a |PayloadBuffer| using the allocator callback.
  // Returns nullptr if the allocation fails.
  fbl::RefPtr<PayloadBuffer> AllocateUsingAllocateCallback(uint64_t size) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable std::mutex mutex_;

  Connector output_ FXL_GUARDED_BY(mutex_);
  Connector input_ FXL_GUARDED_BY(mutex_);

  // Optionally provided by the input to perform allocations against the input
  // VMOS.
  AllocateCallback allocate_callback_;

  // Indicates whether copying must occur. If this field is true, the input
  // will have an allocator.
  bool copy_ FXL_GUARDED_BY(mutex_) = false;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_
