// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/macros.h>

#include <mutex>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/graph/payloads/local_memory_payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_config.h"
#include "src/media/playback/mediaplayer/graph/payloads/vmo_payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"

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
// Methods may be called on any thread unless otherwise noted in the method comments.
class PayloadManager {
 public:
  PayloadManager();

  ~PayloadManager() = default;

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
  using AllocateCallback = fit::function<fbl::RefPtr<PayloadBuffer>(uint64_t, const PayloadVmos&)>;

  // Dumps this |PayloadManager|'s state to |os|.
  void Dump(std::ostream& os) const;

  // Register callbacks to call when the connection is ready.
  void RegisterReadyCallbacks(fit::closure output, fit::closure input);

  // Register callbacks to call when the sysmem tokens have been replaced. These are only called
  // when old tokens are being replaced. The first token for a node is available immediately after
  // the node configures the connector.
  void RegisterNewSysmemTokenCallbacks(fit::closure output, fit::closure input);

  // Applies the output configuration supplied in |config|.
  //
  // This method must be called on the main graph thread.
  void ApplyOutputConfiguration(const PayloadConfig& config,
                                ServiceProvider* service_provider = nullptr);

  // Applies the input configuration supplied in |config|.
  //
  // |allocate_callback| may be supplied for VMO modes only. It allows the node
  // to perform the actual allocations against the VMOs. The allocator callback
  // will never be asked to allocate from VMOs provided by the output.
  // |allocate_callback| is called on an arbitrary thread, and may not reenter
  // this |PayloadManager|.
  //
  // This method must be called on the main graph thread.
  void ApplyInputConfiguration(const PayloadConfig& config, AllocateCallback allocate_callback,
                               ServiceProvider* service_provider = nullptr);

  // Indicates whether the connection manager is ready for allocator access.
  bool ready() const;

  // Allocates and returns a |PayloadBuffer| for the output with the specified
  // size. Returns nullptr if the allocation fails.
  fbl::RefPtr<PayloadBuffer> AllocatePayloadBufferForOutput(uint64_t size) const;

  // Gets the |PayloadVmos| interface for the input. This method should only be
  // called if this |PayloadManager| is ready and the input mode is |kUsesVmos|
  // or |kProvidesVmos|.
  PayloadVmos& input_vmos() const;

  // Gets the |PayloadVmoProvision| interface for the input. This method should
  // only be called if this |PayloadManager| is ready and the input mode is
  // |kProvidesVmos|.
  PayloadVmoProvision& input_external_vmos() const;

  // Takes the |BufferCollectionTokenPtr| for the input. This method should only be called if this
  // |PayloadManager| is ready and the input mode is |kUsesSysmemVmos|.
  fuchsia::sysmem::BufferCollectionTokenPtr TakeInputSysmemToken();

  // Gets the |PayloadVmos| interface for the output. This method should only be
  // called if this |PayloadManager| is ready and the output mode is |kUsesVmos|
  // or |kProvidesVmos|.
  PayloadVmos& output_vmos() const;

  // Gets the |PayloadVmoProvision| interface for the output. This method should
  // only be called if this |PayloadManager| is ready and the output mode is
  // |kProvidesVmos|.
  PayloadVmoProvision& output_external_vmos() const;

  // Takes the |BufferCollectionTokenPtr| for the output. This method should only be called if this
  // |PayloadManager| is ready and the output mode is |kUsesSysmemVmos|.
  fuchsia::sysmem::BufferCollectionTokenPtr TakeOutputSysmemToken();

  // Indicates whether copying is required and maybe provides a copy destination
  // payload buffer. This method returns true if and only if copying is required
  // for this connection. If copying is required and |size| is non-zero, this
  // method will attempt to allocate a payload buffer into which |size| bytes
  // of payload may be copied. If this method returns true, |size| is non-zero
  // and |*payload_buffer_out| is nullptr after the method returns, this
  // indicates that payload memory for this purpose is exhausted.
  //
  // |payload_buffer_out| may only be nullptr if |size| is also zero.
  bool MaybeAllocatePayloadBufferForCopy(uint64_t size,
                                         fbl::RefPtr<PayloadBuffer>* payload_buffer_out) const;

  // Signals that the output and input are disconnected.
  void OnDisconnect();

  // TEST ONLY.
  // Returns a pointer to the |VmoPayloadAllocator| used to satisfy calls to |input_vmos| or
  // |input_external_vmos|, if there is one, otherwise nullptr.
  VmoPayloadAllocator* input_vmo_payload_allocator_for_testing() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return input_vmo_payload_allocator_locked();
  }

  // TEST ONLY.
  // Returns a pointer to the |VmoPayloadAllocator| used to satisfy calls to |output_vmos| or
  // |output_external_vmos|, if there is one, otherwise nullptr.
  VmoPayloadAllocator* output_vmo_payload_allocator_for_testing() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return output_vmo_payload_allocator_locked();
  }

  // TEST ONLY.
  // Returns a pointer to the |LocalMemoryPayloadAllocator| used to allocate memory for the output,
  // if there is one, otherwise nullptr.
  LocalMemoryPayloadAllocator* output_local_memory_payload_allocator_for_testing() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return output_.local_memory_allocator_.get();
  }

  // TEST ONLY.
  // Indicates whether this |PayloadManager| must copy payloads.
  bool must_copy_for_testing() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return copy_;
  }

 private:
  // State relating to output or input.
  struct Connector {
    // Ensure that this |Connector| has no allocators.
    void EnsureNoAllocator();

    // Ensure that this |Connector| has a local memory allocator.
    void EnsureLocalMemoryAllocator();

    // Ensure that this |Connector| has a VMO allocator with no VMOs.
    void EnsureEmptyVmoAllocator(VmoAllocation vmo_allocation);

    // Ensures this |Connector| has a VMO allocator prepared to allocate from externally-provided
    // VMOs. If |vmo_allocation| is not provided, the |VmoAllocation| value from |config_| is used.
    void EnsureExternalVmoAllocator(VmoAllocation vmo_allocation = VmoAllocation::kNotApplicable);

    // Ensures this |Connector| has a VMO allocator provisioned with VMOs as specified in |config|.
    //
    // This method is used in three cases:
    // 1) The allocator is associated with only the output, in which case |config| is the config for
    //    that output.
    // 2) The allocator is associated with only the input, in which case |config| is the augmented
    //    config for that input (|AugmentedInputConfig()|).
    // 3) The allocator is shared between the output and input, in which case |config| is the
    //    merged configuration of the output and the input (|CombinedConfig()|).
    void EnsureProvisionedVmoAllocator(const PayloadConfig& config);

    // Ensures this |Connector| has a VMO allocator prepared to allocate from sysmem-provided VMOs.
    //
    // |local_config| is used to determine the buffer constraints that are sent to the sysmem buffer
    // collection. Those constraints concern the needs of the *local* end of the connection. The
    // end of the connection that uses sysmem itself supplies its constraints directly to sysmem.
    // For example, if the upstream node uses sysmem and the downstream node wants to access
    // payloads locally, the upstream node (associated with the output) will be providing its own
    // constraints to sysmem directly, and |local_config| should reflect the constraints of the
    // downstream node (associated with the input). In this case, |local_config| should be
    // |input_.config_|, event though it's the output side that is using sysmem.
    //
    // |vmo_allocation| indicates how payload will be allocated from VMOs locally. In many cases,
    // no such allocation will occur, in which case the default is appropriate. If allocation does
    // occur, it must meet the constraints of the connector using sysmem.
    //
    // Sometime after this method is called, the owner's |DecrementReadyDeferrals| is called.
    // The owning |PayloadManager| should increment |ready_deferrals_| before calling this method.
    void EnsureProvisionedSysmemVmoAllocator(
        const PayloadConfig& local_config, fuchsia::sysmem::Allocator* sysmem_allocator,
        VmoAllocation vmo_allocation = VmoAllocation::kNotApplicable);

    // Return a |PayloadAllocator| implemented by this connector, if there is
    // one, nullptr otherwise.
    PayloadAllocator* payload_allocator() const;

    PayloadManager* owner_;
    PayloadConfig config_;
    fbl::RefPtr<LocalMemoryPayloadAllocator> local_memory_allocator_;
    fbl::RefPtr<VmoPayloadAllocator> vmo_allocator_;
    // |sysmem_token_for_node_| is the one provided to the node for its use.
    fuchsia::sysmem::BufferCollectionTokenPtr sysmem_token_for_node_;
    // |sysmem_token_for_mate_or_provisioning_| either becomes |sysmem_token_for_node_| for the
    // other connector or is used to provision |vmo_allocator_| with buffers.
    fuchsia::sysmem::BufferCollectionTokenPtr sysmem_token_for_mate_or_provisioning_;
    fuchsia::sysmem::BufferCollectionPtr sysmem_collection_;
    // Incremented when sysmem_token_for_node_ is set (not cleared).
    uint32_t sysmem_token_generation_ = 0;
  };

  void DumpInternal(std::ostream& os) const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Indicates whether the connection manager is ready for allocator access.
  bool ready_locked() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  VmoPayloadAllocator* input_vmo_payload_allocator_locked() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    FX_DCHECK(ready_locked());
    return input_.vmo_allocator_ ? input_.vmo_allocator_.get() : output_.vmo_allocator_.get();
  }

  // TEST ONLY.
  VmoPayloadAllocator* output_vmo_payload_allocator_locked() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    FX_DCHECK(ready_locked());
    return output_.vmo_allocator_ ? output_.vmo_allocator_.get() : input_.vmo_allocator_.get();
  }

  // Ensures that the connector has a pair of buffer collection tokens.
  void EnsureBufferCollectionTokens(Connector* connector) FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Share |from|'s buffer collection with |to| creating a duplicate token |dup|.
  void ShareBufferCollection(Connector* from, Connector* to,
                             fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> dup)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Decrements |ready_deferrals_| and signals readiness if this |PayloadManager| is ready.
  //
  // This method must be called on the main graph thread.
  void DecrementReadyDeferrals() FXL_LOCKS_EXCLUDED(mutex_);

  // Ensures that |sysmem_allocator_| is populated.
  //
  // This method must be called on the main graph thread.
  void EnsureSysmemAllocator() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Updates the allocators based in the current configs.
  //
  // This method must be called on the main graph thread.
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
  VmoAllocation CombinedVmoAllocation() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns a |PayloadConfig| that combines both output an input payload configs. The output and
  // input must have compatible |config_.vmo_allocation_| values.
  PayloadConfig CombinedConfig() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns the input's |PayloadConfig| with the |max_payload_size_| value set to the max of those
  // values for input and output.
  PayloadConfig AugmentedInputConfig() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns the output's |PayloadConfig| with the |max_payload_count_| value set to the zero and
  // |map_flags_| set to |ZX_VM_PERM_WRITE|.
  PayloadConfig CopyToOutputConfig() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Allocates and returns a |PayloadBuffer| using the allocator callback.
  // Returns nullptr if the allocation fails.
  fbl::RefPtr<PayloadBuffer> AllocateUsingAllocateCallback(uint64_t size) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  FIT_DECLARE_THREAD_CHECKER(thread_checker_);
  mutable std::mutex mutex_;

  Connector output_ FXL_GUARDED_BY(mutex_);
  Connector input_ FXL_GUARDED_BY(mutex_);

  // Optionally provided by the input to perform allocations against the input
  // VMOS.
  AllocateCallback allocate_callback_ FXL_GUARDED_BY(mutex_);

  // Indicates whether copying must occur. If this field is true, the input
  // will have an allocator.
  bool copy_ FXL_GUARDED_BY(mutex_) = false;

  ServiceProvider* service_provider_ FXL_GUARDED_BY(mutex_) = nullptr;

  // Accessed only on the main graph thread.
  fuchsia::sysmem::AllocatorPtr sysmem_allocator_ FXL_GUARDED_BY(mutex_);

  // Async callbacks for readiness. Accessed only on the main graph thread.
  fit::closure ready_callback_for_output_;
  fit::closure ready_callback_for_input_;

  // Async callbacks for when sysmem tokens have been replaced. Accessed only on the main graph
  // thread.
  fit::closure new_sysmem_token_callback_for_output_;
  fit::closure new_sysmem_token_callback_for_input_;

  // Count of reasons to defer readiness. This |PayloadManager| is ready when this value reaches
  // zero and neither config mode is |kNotConfigured|. |ApplyOutputConfiguration| and
  // |ApplyInputConfiguration| both increment this value on entry and decrement it on exit.
  // Operations that defer readiness (e.g. |EnsureProvisionedSysmemVmoAllocator|) increment this
  // value during |Apply*Configuration| and decrement it when complete.
  uint32_t ready_deferrals_ FXL_GUARDED_BY(mutex_) = 0;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_MANAGER_H_
