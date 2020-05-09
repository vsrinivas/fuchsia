// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/zx/channel.h>

#include <list>
#include <map>
#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "device.h"

namespace sysmem_driver {

class BufferCollectionToken;
class BufferCollection;
class MemoryAllocator;
class LogicalBufferCollection : public fbl::RefCounted<LogicalBufferCollection> {
 public:
  using Constraints =
      FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                 &llcpp::fuchsia::sysmem::fuchsia_sysmem_BufferCollectionConstraintsTable>;
  using ImageFormatConstraints =
      FidlStruct<fuchsia_sysmem_ImageFormatConstraints,
                 &llcpp::fuchsia::sysmem::fuchsia_sysmem_ImageFormatConstraintsTable>;
  using BufferCollectionInfo =
      FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                 &llcpp::fuchsia::sysmem::fuchsia_sysmem_BufferCollectionInfo_2Table>;

  ~LogicalBufferCollection();

  static void Create(zx::channel buffer_collection_token_request, Device* parent_device);

  // |parent_device| the Device* that the calling allocator is part of.  The
  // tokens_by_koid_ for each Device is separate.  If somehow two clients were
  // to get connected to two separate sysmem device instances hosted in the
  // same devhost, those clients (intentionally) won't be able to share a
  // LogicalBufferCollection.
  //
  // |buffer_collection_token| the client end of the BufferCollectionToken
  // being turned in by the client to get a BufferCollection in exchange.
  //
  // |buffer_collection_request| the server end of a BufferCollection channel
  // to be served by the LogicalBufferCollection associated with
  // buffer_collection_token.
  static void BindSharedCollection(Device* parent_device, zx::channel buffer_collection_token,
                                   zx::channel buffer_collection_request);

  // ZX_OK if the token is known to the server.
  // ZX_ERR_NOT_FOUND if the token isn't known to the server.
  static zx_status_t ValidateBufferCollectionToken(Device* parent_device,
                                                   zx_koid_t token_server_koid);

  // This is used to create the initial BufferCollectionToken, and also used
  // by BufferCollectionToken::Duplicate().
  //
  // The |self| parameter exists only because LogicalBufferCollection can't
  // hold a std::weak_ptr<> to itself because that requires libc++ (the binary
  // not just the headers) which isn't available in Zircon so far.
  void CreateBufferCollectionToken(fbl::RefPtr<LogicalBufferCollection> self,
                                   uint32_t rights_attenuation_mask,
                                   zx::channel buffer_collection_token_request);

  void OnSetConstraints();

  struct AllocationResult {
    const fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection_info = nullptr;
    const zx_status_t status = ZX_OK;
  };
  AllocationResult allocation_result();

  Device* parent_device() const { return parent_device_; }

 private:
  LogicalBufferCollection(Device* parent_device);

  // If |format| is nonnull, will log an error. This also cleans out a lot of
  // state that's unnecessary after a failure.
  void Fail(const char* format, ...);

  static void LogInfo(const char* format, ...);

  static void LogError(const char* format, ...);

  void MaybeAllocate();

  void TryAllocate();

  void SetFailedAllocationResult(zx_status_t status);

  void SetAllocationResult(BufferCollectionInfo info);

  void SendAllocationResult();

  void BindSharedCollectionInternal(BufferCollectionToken* token,
                                    zx::channel buffer_collection_request);

  bool CombineConstraints();

  bool CheckSanitizeBufferCollectionConstraints(
      fuchsia_sysmem_BufferCollectionConstraints* constraints, bool is_aggregated);

  bool CheckSanitizeBufferMemoryConstraints(fuchsia_sysmem_BufferMemoryConstraints* constraints);

  bool CheckSanitizeImageFormatConstraints(fuchsia_sysmem_ImageFormatConstraints* constraints);

  Constraints BufferCollectionConstraintsClone(
      const fuchsia_sysmem_BufferCollectionConstraints* input);

  ImageFormatConstraints ImageFormatConstraintsClone(
      const fuchsia_sysmem_ImageFormatConstraints* input);

  bool AccumulateConstraintBufferCollection(fuchsia_sysmem_BufferCollectionConstraints* acc,
                                            const fuchsia_sysmem_BufferCollectionConstraints* c);

  bool AccumulateConstraintHeapPermitted(uint32_t* acc_count, fuchsia_sysmem_HeapType acc[],
                                         uint32_t c_count, const fuchsia_sysmem_HeapType c[]);

  bool AccumulateConstraintBufferMemory(fuchsia_sysmem_BufferMemoryConstraints* acc,
                                        const fuchsia_sysmem_BufferMemoryConstraints* c);

  bool AccumulateConstraintImageFormats(uint32_t* acc_count,
                                        fuchsia_sysmem_ImageFormatConstraints acc[],
                                        uint32_t c_count,
                                        const fuchsia_sysmem_ImageFormatConstraints c[]);

  bool AccumulateConstraintImageFormat(fuchsia_sysmem_ImageFormatConstraints* acc,
                                       const fuchsia_sysmem_ImageFormatConstraints* c);

  bool AccumulateConstraintColorSpaces(uint32_t* acc_count, fuchsia_sysmem_ColorSpace acc[],
                                       uint32_t c_count, const fuchsia_sysmem_ColorSpace c[]);

  bool IsColorSpaceEqual(const fuchsia_sysmem_ColorSpace& a, const fuchsia_sysmem_ColorSpace& b);

  BufferCollectionInfo Allocate(zx_status_t* allocation_result);

  zx_status_t AllocateVmo(MemoryAllocator* allocator,
                          const fuchsia_sysmem_SingleBufferSettings* settings, zx::vmo* vmo);

  int32_t CompareImageFormatConstraintsTieBreaker(const fuchsia_sysmem_ImageFormatConstraints* a,
                                                  const fuchsia_sysmem_ImageFormatConstraints* b);

  int32_t CompareImageFormatConstraintsByIndex(uint32_t index_a, uint32_t index_b);

  Device* parent_device_ = nullptr;

  using TokenMap = std::map<BufferCollectionToken*, std::unique_ptr<BufferCollectionToken>>;
  TokenMap token_views_;

  using CollectionMap = std::map<BufferCollection*, std::unique_ptr<BufferCollection>>;
  CollectionMap collection_views_;

  using ConstraintsList = std::list<Constraints>;
  ConstraintsList constraints_list_;

  bool is_allocate_attempted_ = false;

  Constraints constraints_{Constraints::Null};

  // Iff true, initial allocation has been attempted and has succeeded or
  // failed.  Both allocation_result_status_ and allocation_result_info_ are
  // not meaningful until has_allocation_result_ is true.
  bool has_allocation_result_ = false;
  zx_status_t allocation_result_status_ = ZX_OK;
  BufferCollectionInfo allocation_result_info_{BufferCollectionInfo::Null};

  MemoryAllocator* memory_allocator_ = nullptr;

  // We keep LogicalBufferCollection alive as long as there are child VMOs
  // outstanding (no revoking of child VMOs for now).
  //
  // This tracking is for the benefit of MemoryAllocator sub-classes that need
  // a Delete() call, such as to clean up a slab allocation and/or to inform
  // an external allocator of delete.
  class TrackedParentVmo {
   public:
    using DoDelete = fit::callback<void(TrackedParentVmo* parent)>;
    // The do_delete callback will be invoked upon the sooner of (A) the client
    // code causing ~ParentVmo, or (B) ZX_VMO_ZERO_CHILDREN occurring async
    // after StartWait() is called.
    TrackedParentVmo(fbl::RefPtr<LogicalBufferCollection> buffer_collection, zx::vmo vmo,
                     DoDelete do_delete);
    ~TrackedParentVmo();

    // This should only be called after client code has created a child VMO, and
    // will begin the wait for ZX_VMO_ZERO_CHILDREN.
    zx_status_t StartWait(async_dispatcher_t* dispatcher);

    // Cancel the wait. This should only be used by LogicalBufferCollection
    zx_status_t CancelWait();

    zx::vmo TakeVmo();
    [[nodiscard]] const zx::vmo& vmo() const;

    void set_child_koid(zx_koid_t koid) { child_koid_ = koid; }

    TrackedParentVmo(const TrackedParentVmo&) = delete;
    TrackedParentVmo(TrackedParentVmo&&) = delete;
    TrackedParentVmo& operator=(const TrackedParentVmo&) = delete;
    TrackedParentVmo& operator=(TrackedParentVmo&&) = delete;

   private:
    void OnZeroChildren(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
    fbl::RefPtr<LogicalBufferCollection> buffer_collection_;
    zx::vmo vmo_;
    zx_koid_t child_koid_{};
    DoDelete do_delete_;
    async::WaitMethod<TrackedParentVmo, &TrackedParentVmo::OnZeroChildren> zero_children_wait_;
    // Only for asserts:
    bool waiting_ = {};
  };
  using ParentVmoMap = std::map<zx_handle_t, std::unique_ptr<TrackedParentVmo>>;
  ParentVmoMap parent_vmos_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
