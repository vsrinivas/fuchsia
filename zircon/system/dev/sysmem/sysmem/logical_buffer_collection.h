// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
#define ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_

#include "device.h"

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/zx/channel.h>

#include <list>
#include <map>
#include <memory>

extern const fidl_type_t fuchsia_sysmem_BufferCollectionConstraintsTable;
extern const fidl_type_t fuchsia_sysmem_ImageFormatConstraintsTable;
extern const fidl_type_t fuchsia_sysmem_BufferCollectionInfo_2Table;

class BufferCollectionToken;
class BufferCollection;
class MemoryAllocator;
class LogicalBufferCollection
    : public fbl::RefCounted<LogicalBufferCollection> {
public:
    using Constraints =
        FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                   &fuchsia_sysmem_BufferCollectionConstraintsTable>;
    using ImageFormatConstraints =
        FidlStruct<fuchsia_sysmem_ImageFormatConstraints,
                   &fuchsia_sysmem_ImageFormatConstraintsTable>;
    using BufferCollectionInfo =
        FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                   &fuchsia_sysmem_BufferCollectionInfo_2Table>;

    ~LogicalBufferCollection();

    static void Create(zx::channel buffer_collection_token_request,
                       Device* parent_device);

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
    static void BindSharedCollection(Device* parent_device,
                                     zx::channel buffer_collection_token,
                                     zx::channel buffer_collection_request);

    // This is used to create the initial BufferCollectionToken, and also used
    // by BufferCollectionToken::Duplicate().
    //
    // The |self| parameter exists only because LogicalBufferCollection can't
    // hold a std::weak_ptr<> to itself because that requires libc++ (the binary
    // not just the headers) which isn't available in Zircon so far.
    void
    CreateBufferCollectionToken(fbl::RefPtr<LogicalBufferCollection> self,
                                uint32_t rights_attenuation_mask,
                                zx::channel buffer_collection_token_request);

    void OnSetConstraints();

    struct AllocationResult {
        const fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection_info =
            nullptr;
        const zx_status_t status = ZX_OK;
    };
    AllocationResult allocation_result();

private:
    LogicalBufferCollection(Device* parent_device);

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
        fuchsia_sysmem_BufferCollectionConstraints* constraints);

    bool CheckSanitizeBufferMemoryConstraints(
        fuchsia_sysmem_BufferMemoryConstraints* constraints);

    bool CheckSanitizeImageFormatConstraints(
        fuchsia_sysmem_ImageFormatConstraints* constraints);

    Constraints BufferCollectionConstraintsClone(
        const fuchsia_sysmem_BufferCollectionConstraints* input);

    ImageFormatConstraints ImageFormatConstraintsClone(
        const fuchsia_sysmem_ImageFormatConstraints* input);

    bool AccumulateConstraintBufferCollection(
        fuchsia_sysmem_BufferCollectionConstraints* acc,
        const fuchsia_sysmem_BufferCollectionConstraints* c);

    bool AccumulateConstraintHeapPermitted(uint32_t* acc_count,
                                           fuchsia_sysmem_HeapType acc[],
                                           uint32_t c_count,
                                           const fuchsia_sysmem_HeapType c[]);

    bool AccumulateConstraintBufferMemory(
        fuchsia_sysmem_BufferMemoryConstraints* acc,
        const fuchsia_sysmem_BufferMemoryConstraints* c);

    bool AccumulateConstraintImageFormats(
        uint32_t* acc_count, fuchsia_sysmem_ImageFormatConstraints acc[],
        uint32_t c_count, const fuchsia_sysmem_ImageFormatConstraints c[]);

    bool AccumulateConstraintImageFormat(
        fuchsia_sysmem_ImageFormatConstraints* acc,
        const fuchsia_sysmem_ImageFormatConstraints* c);

    bool AccumulateConstraintColorSpaces(uint32_t* acc_count,
                                         fuchsia_sysmem_ColorSpace acc[],
                                         uint32_t c_count,
                                         const fuchsia_sysmem_ColorSpace c[]);

    bool IsColorSpaceEqual(const fuchsia_sysmem_ColorSpace& a,
                           const fuchsia_sysmem_ColorSpace& b);

    BufferCollectionInfo Allocate(zx_status_t* allocation_result);

    zx_status_t AllocateVmo(MemoryAllocator* allocator,
                            const fuchsia_sysmem_SingleBufferSettings* settings,
                            zx::vmo* vmo);

    int32_t CompareImageFormatConstraintsTieBreaker(
        const fuchsia_sysmem_ImageFormatConstraints* a,
        const fuchsia_sysmem_ImageFormatConstraints* b);

    int32_t CompareImageFormatConstraintsByIndex(uint32_t index_a,
                                                 uint32_t index_b);

    Device* parent_device_ = nullptr;

    using TokenMap = std::map<BufferCollectionToken*,
                              std::unique_ptr<BufferCollectionToken>>;
    TokenMap token_views_;

    using CollectionMap =
        std::map<BufferCollection*, std::unique_ptr<BufferCollection>>;
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
};

#endif // ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
