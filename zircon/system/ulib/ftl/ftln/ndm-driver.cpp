// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl/ndm-driver.h>

#include <zircon/assert.h>

#include "kprivate/fsdriver.h"
#include "kprivate/fsprivate.h"
#include "kprivate/ndm.h"
#include "posix.h"

namespace ftl {

namespace {

bool g_init_performed = false;

// Implementation of the driver interface:

// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int ReadPagesImpl(uint32_t page, uint32_t count, uint8_t* data, uint8_t* spare, void* dev) {
    NdmDriver* device = reinterpret_cast<NdmDriver*>(dev);
    return device->NandRead(page, count, data, spare);
}

// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int ReadPages(uint32_t page, uint32_t count, uint8_t* data, uint8_t* spare, void* dev) {
   return ReadPagesImpl(page, count, data, nullptr, dev);
}

// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int ReadPage(uint32_t page, uint8_t* data, uint8_t* spare, void* dev) {
    return ReadPagesImpl(page, 1, data, nullptr, dev);
}

// Returns kNdmOk or kNdmError on ECC decode failure.
int ReadSpare(uint32_t page, uint8_t* spare, void* dev) {
    int result = ReadPagesImpl(page, 1, nullptr, spare, dev);
    if (result == kNdmFatalError || result == kNdmUncorrectableEcc) {
        return kNdmError;
    }

    // kNdmUnsafeEcc is also OK as the data is still correct.
    return kNdmOk;
}

// Returns kNdmOk or kNdmError.
int ReadSpareNoEcc(uint32_t page, uint8_t* spare, void* dev) {
    int result = ReadPagesImpl(page, 1, nullptr, spare, dev);
    return result == kNdmFatalError ? kNdmError : kNdmOk;
}

// Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking the block as bad.
int WritePages(uint32_t page, uint32_t count, const uint8_t* data, uint8_t* spare, int action,
               void* dev) {
    NdmDriver* device = reinterpret_cast<NdmDriver*>(dev);
    return device->NandWrite(page, count, data, spare);
}

// Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking the block as bad.
int WritePage(uint32_t page, const uint8_t* data, uint8_t* spare, int action, void* dev) {
    return WritePages(page, 1, data, spare, action, dev);
}

// Returns kNdmOk or kNdmError. kNdmError triggers marking the block as bad.
int EraseBlock(uint32_t page, void* dev) {
    NdmDriver* device = reinterpret_cast<NdmDriver*>(dev);
    return device->NandErase(page);
}

// Returns kTrue, kFalse or kNdmError.
int IsBadBlockImpl(uint32_t page, void* dev) {
    NdmDriver* device = reinterpret_cast<NdmDriver*>(dev);
    return device->IsBadBlock(page);
}

// Returns kTrue or kFalse (kFalse on error).
int IsEmpty(uint32_t page, uint8_t* data, uint8_t* spare, void* dev) {
    int result = ReadPagesImpl(page, 1, data, spare, dev);

    // kNdmUncorrectableEcc and kNdmUnsafeEcc are ok.
    if (result == kNdmFatalError) {
        return kFalse;
    }

    NdmDriver* device = reinterpret_cast<NdmDriver*>(dev);
    return device->IsEmptyPage(page, data, spare) ? kTrue : kFalse;
}

// Returns kNdmOk or kNdmError.
int CheckPage(uint32_t page, uint8_t* data, uint8_t* spare, int* status, void* dev) {
    *status = IsEmpty(page, data, spare, dev) ? NDM_PAGE_ERASED : NDM_PAGE_VALID;
    return kNdmOk;
}

}  // namespace

NdmBaseDriver::~NdmBaseDriver() {
    RemoveNdmVolume();
}

bool NdmBaseDriver::IsNdmDataPresent(const VolumeOptions& options) {
    NDMDrvr driver = {};
    driver.num_blocks = options.num_blocks;
    driver.max_bad_blocks = options.max_bad_blocks;
    driver.block_size = options.block_size;
    driver.page_size = options.page_size;
    driver.eb_size = options.eb_size;
    driver.flags = FSF_MULTI_ACCESS | FSF_FREE_SPARE_ECC | options.flags;
    driver.dev = this;
    driver.type = NDM_SLC;
    driver.read_pages = ReadPages;
    driver.write_pages = WritePages;
    driver.write_data_and_spare = WritePage;
    driver.read_decode_data = ReadPage;
    driver.read_decode_spare = ReadSpare;
    driver.read_spare = ReadSpareNoEcc;
    driver.data_and_spare_erased = IsEmpty;
    driver.data_and_spare_check = CheckPage;
    driver.erase_block = EraseBlock;
    driver.is_block_bad = IsBadBlockImpl;

    SetFsErrCode(NDM_OK);
    ndm_ = ndmAddDev(&driver);
    return ndm_ || GetFsErrCode() != NDM_NO_META_BLK;
}

bool NdmBaseDriver::BadBbtReservation() const {
    if (ndm_) {
        return false;
    }
    FsErrorCode error = static_cast<FsErrorCode>(GetFsErrCode());
    switch (error) {
        case NDM_TOO_MANY_IBAD:
        case NDM_TOO_MANY_RBAD:
        case NDM_RBAD_LOCATION:
            return true;
        default:
            return false;
    }
}

const char* NdmBaseDriver::CreateNdmVolume(const Volume* ftl_volume, const VolumeOptions& options) {
    if (!ndm_) {
        IsNdmDataPresent(options);
    }

    if (!ndm_) {
        return "ndmAddDev failed";
    }

    if (ndmSetNumPartitions(ndm_, 1) != 0) {
        return "ndmSetNumPartitions failed";
    }

    NDMPartition partition = {};
    partition.num_blocks = ndmGetNumVBlocks(ndm_) - partition.first_block;
    partition.type = XFS_VOL;

    if (ndmWritePartition(ndm_, &partition, 0, "ftl") != 0) {
        return "ndmWritePartition failed";
    }

    FtlNdmVol ftl = {};
    XfsVol xfs = {};

    ftl.flags = FSF_EXTRA_FREE;
    ftl.cached_map_pages = options.num_blocks * (options.block_size / options.page_size);
    ftl.extra_free = 6;  // Over-provision 6% of the device.
    xfs.ftl_volume = const_cast<Volume*>(ftl_volume);

    if (ndmAddVolXfsFTL(ndm_, 0, &ftl, &xfs) != 0) {
        return "ndmAddVolXfsFTL failed";
    }

    return nullptr;
}

bool NdmBaseDriver::RemoveNdmVolume() {
    if (ndm_ && ndmDelDev(ndm_) == 0) {
        ndm_ = nullptr;
        return true;
    }
    return false;
}

bool NdmBaseDriver::SaveBadBlockData() {
    return ndmExtractBBL(ndm_) >= 0 ? true : false;
}

bool NdmBaseDriver::RestoreBadBlockData() {
    return ndmInsertBBL(ndm_) == 0 ? true : false;
}

bool NdmBaseDriver::IsEmptyPageImpl(const uint8_t* data, uint32_t data_len, const uint8_t* spare,
                                    uint32_t spare_len) const {
    const int64_t* pointer = reinterpret_cast<const int64_t*>(data);
    ZX_DEBUG_ASSERT(data_len % sizeof(*pointer) == 0);
    for (size_t i = 0; i < data_len / sizeof(*pointer); i++) {
        if (pointer[i] != -1) {
          return false;
        }
    }

    ZX_DEBUG_ASSERT(spare_len % sizeof(*pointer) == 0);
    pointer = reinterpret_cast<const int64_t*>(spare);
    for (size_t i = 0; i < spare_len / sizeof(*pointer); i++) {
        if (pointer[i] != -1) {
          return false;
        }
    }
    return true;
}

bool InitModules() {
    if (!g_init_performed) {
        // Unfortunately, module initialization is a global affair, and there is
        // no cleanup. At least, make sure no re-initialization takes place.
        if (NdmInit() != 0 || FtlInit() != 0) {
            return false;
        }
        g_init_performed = true;
    }
    return true;
}

}  // namespace ftl
