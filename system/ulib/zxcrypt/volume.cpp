// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/hkdf.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <lib/zx/vmo.h>
#include <zxcrypt/volume.h>

#define ZXDEBUG 0

namespace zxcrypt {

// Several copies of the metadata for a zxcrypt volume is saved at the beginning and end of the
// devices.  The number of copies is given by |kMetadataBlocks * kReservedSlices|, and the locations
// of each block can be iterated through using |Begin| and |Next|.  The metadata block, or
// superblock, consists of a fixed type GUID, an instance GUID, a 32-bit version, a set of "key
// slots"  The key slots are data cipher key material encrypted with a wrapping crypto::AEAD key
// derived from the caller-provided root key and specific slot.

// Determines what algorithms are in use when creating new zxcrypt devices.
const Volume::Version Volume::kDefaultVersion = Volume::kAES128_CTR_SHA256;

// Maximum number of key slots.  If a device's block size can not hold |kNumSlots| for a particular
// version, then attempting to |Create| or |Open| a zxcrypt volume will fail with
// |ZX_ERR_NOT_SUPPORTED|.
const slot_num_t Volume::kNumSlots = 16;

// The number of FVM-like slices reserved at the start of the device, each holding |kMetadataBlocks|
// copies of the superblock.
const size_t Volume::kReservedSlices = 2;

namespace {

// The number of metadata blocks in a reserved metadata slice, each holding a copy of the
// superblock.
const size_t kMetadataBlocks = 2;

// HKDF labels
const size_t kMaxLabelLen = 16;
const char* kWrapKeyLabel = "wrap key %" PRIu64;
const char* kWrapIvLabel = "wrap iv %" PRIu64;

// Header is type GUID | instance GUID | version.
const size_t kHeaderLen = GUID_LEN + GUID_LEN + sizeof(uint32_t);

void SyncComplete(block_op_t* block, zx_status_t status) {
    // Use the 32bit command field to shuttle the response back to the callsite that's waiting on
    // the completion
    block->command = status;
    completion_signal(static_cast<completion_t*>(block->cookie));
}

// Performs synchronous I/O
zx_status_t SyncIO(zx_device_t* dev, uint32_t cmd, void* buf, size_t off, size_t len) {
    zx_status_t rc;

    if (!dev || !buf || len == 0) {
        xprintf("bad parameter(s): dev=%p, buf=%p, len=%zu\n", dev, buf, len);
        return ZX_ERR_INVALID_ARGS;
    }

    block_protocol_t proto;
    if ((rc = device_get_protocol(dev, ZX_PROTOCOL_BLOCK, &proto)) != ZX_OK) {
        xprintf("block protocol not support\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx::vmo vmo;
    if ((rc = zx::vmo::create(len, 0, &vmo)) != ZX_OK) {
        xprintf("zx::vmo::create failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    block_info_t info;
    size_t op_size;
    proto.ops->query(proto.ctx, &info, &op_size);

    size_t bsz = info.block_size;
    ZX_DEBUG_ASSERT(off / bsz <= UINT32_MAX);
    ZX_DEBUG_ASSERT(len / bsz <= UINT32_MAX);

    char raw[op_size];
    block_op_t* block = reinterpret_cast<block_op_t*>(raw);

    completion_t completion;
    completion_reset(&completion);

    block->command = cmd;
    block->rw.vmo = vmo.get();
    block->rw.length = static_cast<uint32_t>(len / bsz);
    block->rw.offset_dev = static_cast<uint32_t>(off / bsz);
    block->rw.offset_vmo = 0;
    block->rw.pages = nullptr;
    block->completion_cb = SyncComplete;
    block->cookie = &completion;

    if (cmd == BLOCK_OP_WRITE && (rc = vmo.write(buf, 0, len)) != ZX_OK) {
        xprintf("zx::vmo::write failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    proto.ops->queue(proto.ctx, block);
    completion_wait(&completion, ZX_TIME_INFINITE);

    rc = block->command;
    if (rc != ZX_OK) {
        xprintf("Block I/O failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    if (cmd == BLOCK_OP_READ && (rc = vmo.read(buf, 0, len)) != ZX_OK) {
        xprintf("zx::vmo::read failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

} // namespace

Volume::~Volume() {}

// Library methods

zx_status_t Volume::Create(fbl::unique_fd fd, const crypto::Bytes& key) {
    zx_status_t rc;

    if (!fd) {
        xprintf("bad parameter(s): fd=%d\n", fd.get());
        return ZX_ERR_INVALID_ARGS;
    }

    Volume volume(fbl::move(fd));
    if ((rc = volume.Init()) != ZX_OK || (rc = volume.CreateBlock()) != ZX_OK ||
        (rc = volume.SealBlock(key, 0)) != ZX_OK || (rc = volume.CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Volume::Open(fbl::unique_fd fd, const crypto::Bytes& key, slot_num_t slot,
                         fbl::unique_ptr<Volume>* out) {
    zx_status_t rc;

    if (!fd || slot >= kNumSlots || !out) {
        xprintf("bad parameter(s): fd=%d, slot=%" PRIu64 ", out=%p\n", fd.get(), slot, out);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Volume> volume(new (&ac) Volume(fbl::move(fd)));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Volume));
        return ZX_ERR_NO_MEMORY;
    }
    if ((rc = volume->Init()) != ZX_OK || (rc = volume->Open(key, slot)) != ZX_OK) {
        return rc;
    }

    *out = fbl::move(volume);
    return ZX_OK;
}

zx_status_t Volume::Enroll(const crypto::Bytes& key, slot_num_t slot) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!dev_); // Cannot enroll from driver

    if (slot >= kNumSlots) {
        xprintf("bad parameter(s): slot=%" PRIu64 "\n", slot);
        return ZX_ERR_INVALID_ARGS;
    }
    if (!block_.get()) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_BAD_STATE;
    }
    if ((rc = SealBlock(key, slot)) != ZX_OK || (rc = CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Volume::Revoke(slot_num_t slot) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!dev_); // Cannot revoke from driver

    if (slot >= kNumSlots) {
        xprintf("bad parameter(s): slot=%" PRIu64 "\n", slot);
        return ZX_ERR_INVALID_ARGS;
    }
    if (!block_.get()) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_BAD_STATE;
    }
    zx_off_t off = kHeaderLen + (slot_len_ * slot);
    crypto::Bytes invalid;
    if ((rc = invalid.InitRandom(slot_len_)) != ZX_OK ||
        (rc = block_.Copy(invalid, off)) != ZX_OK || (rc = CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Volume::Shred() {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!dev_); // Cannot shred from driver

    if (!block_.get()) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_BAD_STATE;
    }
    if ((rc = block_.Randomize()) != ZX_OK) {
        return rc;
    }
    for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
        if ((rc = Write()) != ZX_OK) {
            return rc;
        }
    }
    Reset();

    return ZX_OK;
}

// Driver methods

zx_status_t Volume::Open(zx_device_t* dev, const crypto::Bytes& key, slot_num_t slot,
                         fbl::unique_ptr<Volume>* out) {
    zx_status_t rc;

    if (!dev || slot >= kNumSlots || !out) {
        xprintf("bad parameter(s): dev=%p, slot=%" PRIu64 ", out=%p\n", dev, slot, out);
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AllocChecker ac;
    fbl::unique_ptr<Volume> volume(new (&ac) Volume(dev));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Volume));
        return ZX_ERR_NO_MEMORY;
    }
    if ((rc = volume->Init()) != ZX_OK || (rc = volume->Open(key, slot)) != ZX_OK) {
        return rc;
    }

    *out = fbl::move(volume);
    return ZX_OK;
}

zx_status_t Volume::GetBlockInfo(block_info_t* out_blk) const {
    if (!block_.get()) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_BAD_STATE;
    }
    if (out_blk) {
        memcpy(out_blk, &blk_, sizeof(blk_));
    }
    return ZX_OK;
}

zx_status_t Volume::GetFvmInfo(fvm_info_t* out_fvm, bool* out_has_fvm) const {
    if (!block_.get()) {
        xprintf("not initialized\n");
        return ZX_ERR_BAD_STATE;
    }
    if (out_fvm) {
        memcpy(out_fvm, &fvm_, sizeof(fvm_));
    }
    if (out_has_fvm) {
        *out_has_fvm = has_fvm_;
    }
    return ZX_OK;
}

zx_status_t Volume::Bind(crypto::Cipher::Direction direction, crypto::Cipher* cipher) const {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(dev_); // Cannot bind from library

    if (!cipher) {
        xprintf("bad parameter(s): cipher=%p\n", cipher);
        return ZX_ERR_INVALID_ARGS;
    }
    if (!block_.get()) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_BAD_STATE;
    }
    if ((rc = cipher->Init(cipher_, direction, data_key_, data_iv_, blk_.block_size)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

// Private methods

Volume::Volume(fbl::unique_fd&& fd) : dev_(nullptr), fd_(fbl::move(fd)) {
    Reset();
}

Volume::Volume(zx_device_t* dev) : dev_(dev), fd_() {
    Reset();
}

// Configuration methods

zx_status_t Volume::Init() {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&] { Reset(); });

    // Get block info; align our blocks to pages
    if ((rc = Ioctl(IOCTL_BLOCK_GET_INFO, nullptr, 0, &blk_, sizeof(blk_))) < 0) {
        xprintf("failed to get block info: %s\n", zx_status_get_string(rc));
        return rc;
    }
    // Sanity check
    uint64_t size;
    if (mul_overflow(blk_.block_size, blk_.block_count, &size)) {
        xprintf("invalid block device: size=%" PRIu32 ", count=%" PRIu64 "\n", blk_.block_size,
                blk_.block_count);
        return ZX_ERR_NOT_SUPPORTED;
    }
    // Adjust block size and count to be page-aligned
    if (blk_.block_size < PAGE_SIZE) {
        if (PAGE_SIZE % blk_.block_size != 0) {
            xprintf("unsupported block size: %u\n", blk_.block_size);
            return ZX_ERR_NOT_SUPPORTED;
        }
        blk_.block_count /= (PAGE_SIZE / blk_.block_size);
        blk_.block_size = PAGE_SIZE;
    } else {
        if (blk_.block_size % PAGE_SIZE != 0) {
            xprintf("unsupported block size: %u\n", blk_.block_size);
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    // Allocate block buffer
    if ((rc = block_.Resize(blk_.block_size)) != ZX_OK) {
        return rc;
    }
    size_t reserved_size;
    if (mul_overflow(blk_.block_size, kMetadataBlocks, &reserved_size)) {
        xprintf("reserved_size overflow size=%" PRIu32 ", kMetadataBlocks=%zu\n", blk_.block_size,
                kMetadataBlocks);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Get FVM info
    switch ((rc = Ioctl(IOCTL_BLOCK_FVM_QUERY, nullptr, 0, &fvm_, sizeof(fvm_)))) {
    case ZX_OK: {
        // This *IS* an FVM partition.
        if (fvm_.slice_size < reserved_size || fvm_.vslice_count <= kReservedSlices) {
            xprintf("bad device: slice_size=%zu, vslice_count=%zu\n", fvm_.slice_size,
                    fvm_.vslice_count);
            return ZX_ERR_NO_SPACE;
        }

        // Ensure first kReservedSlices + 1 slices are allocated
        size_t required = kReservedSlices + 1;
        size_t range = 1;
        query_request_t request;
        query_response_t response;
        extend_request_t extend;
        for (size_t i = 0; i < required; i += range) {
            // Ask about the next contiguous range
            request.count = 1;
            request.vslice_start[0] = i + 1;
            if ((rc = Ioctl(IOCTL_BLOCK_FVM_VSLICE_QUERY, &request, sizeof(request), &response,
                            sizeof(response))) < 0 ||
                response.count == 0 || (range = response.vslice_range[0].count) == 0) {
                xprintf("ioctl_block_fvm_vslice_query failed: %s\n", zx_status_get_string(rc));
                return rc;
            }
            // If already allocated, continue
            if (response.vslice_range[0].allocated) {
                continue;
            };
            // Otherwise, allocate it
            extend.offset = i + 1;
            extend.length = fbl::min(required - i, range);
            if ((rc = Ioctl(IOCTL_BLOCK_FVM_EXTEND, &extend, sizeof(extend), nullptr, 0)) < 0) {
                xprintf("failed to extend FVM partition: %s\n", zx_status_get_string(rc));
                return rc;
            }
        }

        has_fvm_ = true;
        break;
    }

    case ZX_ERR_NOT_SUPPORTED:
        // This is *NOT* an FVM partition.
        if ((blk_.block_count / kReservedSlices) < kMetadataBlocks) {
            xprintf("bad device: block_size=%u, block_count=%" PRIu64 "\n", blk_.block_size,
                    blk_.block_count);
            return ZX_ERR_NO_SPACE;
        }

        // Set "slice" parameters to allow us to pretend it is FVM and use one set
        // of logic.
        fvm_.vslice_count = blk_.block_count / kMetadataBlocks;
        fvm_.slice_size = reserved_size;
        has_fvm_ = false;
        break;

    default:
        // An error occurred
        return rc;
    }

    // Adjust counts to reflect the reserved slices
    fvm_.vslice_count -= kReservedSlices;
    blk_.block_count -= (fvm_.slice_size / blk_.block_size) * kReservedSlices;
    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Volume::Configure(Volume::Version version) {
    zx_status_t rc;

    switch (version) {
    case Volume::kAES256_XTS_SHA256:
        aead_ = crypto::AEAD::kAES128_GCM_SIV;
        cipher_ = crypto::Cipher::kAES256_XTS;
        digest_ = crypto::digest::kSHA256;
        break;
    case Volume::kAES128_CTR_SHA256:
        aead_ = crypto::AEAD::kAES128_GCM_SIV;
        cipher_ = crypto::Cipher::kAES128_CTR;
        digest_ = crypto::digest::kSHA256;
        break;

    default:
        xprintf("unknown version: %u\n", version);
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t wrap_key_len, wrap_iv_len, data_key_len, data_iv_len, tag_len;
    if ((rc = crypto::AEAD::GetKeyLen(aead_, &wrap_key_len)) != ZX_OK ||
        (rc = crypto::AEAD::GetIVLen(aead_, &wrap_iv_len)) != ZX_OK ||
        (rc = crypto::AEAD::GetTagLen(aead_, &tag_len)) != ZX_OK ||
        (rc = crypto::Cipher::GetKeyLen(cipher_, &data_key_len)) != ZX_OK ||
        (rc = crypto::Cipher::GetIVLen(cipher_, &data_iv_len)) != ZX_OK ||
        (rc = crypto::digest::GetDigestLen(digest_, &digest_len_)) != ZX_OK ||
        (rc = wrap_key_.Resize(wrap_key_len)) != ZX_OK ||
        (rc = wrap_iv_.Resize(wrap_iv_len)) != ZX_OK ||
        (rc = data_key_.Resize(data_key_len)) != ZX_OK ||
        (rc = data_iv_.Resize(data_iv_len)) != ZX_OK) {
        return rc;
    }
    slot_len_ = data_key_len + data_iv_len + tag_len;

    size_t total;
    if (mul_overflow(slot_len_, kNumSlots, &total) ||
        add_overflow(total, kHeaderLen, &total)) {
        xprintf("overflow slot_len_=%zu kNumSlots=%zu kHeaderLen=%zu\n",
                slot_len_, kNumSlots, kHeaderLen);
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (blk_.block_size < total) {
        xprintf("block size is too small; have %u, need %zu\n", blk_.block_size,
                total);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_OK;
}

zx_status_t Volume::DeriveSlotKeys(const crypto::Bytes& key, slot_num_t slot) {
    zx_status_t rc;

    crypto::HKDF hkdf;
    char label[kMaxLabelLen];
    if ((rc = hkdf.Init(digest_, key, guid_)) != ZX_OK) {
        return rc;
    }
    snprintf(label, kMaxLabelLen, kWrapKeyLabel, slot);
    if ((rc = hkdf.Derive(label, &wrap_key_)) != ZX_OK) {
        return rc;
    }
    snprintf(label, kMaxLabelLen, kWrapIvLabel, slot);
    if ((rc = hkdf.Derive(label, &wrap_iv_)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

void Volume::Reset() {
    memset(&blk_, 0, sizeof(blk_));
    memset(&fvm_, 0, sizeof(fvm_));
    has_fvm_ = false;
    block_.Reset();
    offset_ = UINT64_MAX;
    guid_.Reset();
    aead_ = crypto::AEAD::kUninitialized;
    wrap_key_.Reset();
    wrap_iv_.Reset();
    cipher_ = crypto::Cipher::kUninitialized;
    data_key_.Reset();
    data_iv_.Reset();
    slot_len_ = 0;
    digest_ = crypto::digest::kUninitialized;
}

// Block methods

zx_status_t Volume::Begin() {
    if (fvm_.slice_size == 0) {
        xprintf("not initialized\n");
        ;
        return ZX_ERR_STOP;
    }
    offset_ = 0;
    return ZX_ERR_NEXT;
}

zx_status_t Volume::Next() {
    offset_ += block_.len();
    size_t slice_offset = offset_ % fvm_.slice_size;
    // If slice isn't complete, move to next block in slice
    if (slice_offset != 0 && slice_offset < fvm_.slice_size) {
        return ZX_ERR_NEXT;
    }
    // Move to next slice
    offset_ -= slice_offset;
    offset_ += fvm_.slice_size;
    return offset_ / fvm_.slice_size < kReservedSlices ? ZX_ERR_NEXT : ZX_ERR_STOP;
}

zx_status_t Volume::CreateBlock() {
    zx_status_t rc;

    // Create a "backdrop" of random data
    if ((rc = block_.Randomize()) != ZX_OK) {
        return rc;
    }

    // Write the variant 1/version 1 type GUID according to RFC 4122.
    uint8_t* out = block_.get();
    memcpy(out, kTypeGuid, GUID_LEN);
    out += GUID_LEN;

    // Create a variant 1/version 4 instance GUID according to RFC 4122.
    if ((rc = guid_.InitRandom(GUID_LEN)) != ZX_OK) {
        return rc;
    }
    guid_[6] = (guid_[6] & 0x0F) | 0x40;
    guid_[8] = (guid_[8] & 0x3F) | 0x80;
    memcpy(out, guid_.get(), GUID_LEN);
    out += GUID_LEN;

    // Write the 32-bit version.
    if ((rc = Configure(kDefaultVersion)) != ZX_OK) {
        return rc;
    }
    uint32_t version = htonl(kDefaultVersion);
    memcpy(out, &version, sizeof(version));

    // Generate the data key and IV, and save the AAD.
    if ((rc = data_key_.Randomize()) != ZX_OK || (rc = data_iv_.Randomize()) != ZX_OK ||
        (rc = header_.Copy(block_.get(), kHeaderLen)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Volume::CommitBlock() {
    zx_status_t rc;

    // Make a copy to compare the read result to; this reduces the number of
    // writes we must do.
    crypto::Bytes block;
    if ((rc = block.Copy(block_)) != ZX_OK) {
        return rc;
    }
    for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
        // Only write back blocks that don't match
        if (Read() == ZX_OK && block_ == block) {
            continue;
        }
        if ((rc = block_.Copy(block)) != ZX_OK || (rc = Write()) != ZX_OK) {
            xprintf("write failed for offset %" PRIu64 ": %s\n", offset_, zx_status_get_string(rc));
        }
    }
    return ZX_OK;
}

zx_status_t Volume::SealBlock(const crypto::Bytes& key, slot_num_t slot) {
    zx_status_t rc;

    // Encrypt the data key
    crypto::AEAD aead;
    crypto::Bytes ptext, ctext;
    zx_off_t off = kHeaderLen + (slot_len_ * slot);
    if ((rc = ptext.Append(data_key_)) != ZX_OK || (rc = ptext.Append(data_iv_)) != ZX_OK ||
        (rc = DeriveSlotKeys(key, slot)) != ZX_OK ||
        (rc = aead.InitSeal(aead_, wrap_key_, wrap_iv_)) != ZX_OK ||
        (rc = aead.SetAD(header_)) != ZX_OK ||
        (rc = aead.Seal(ptext, &wrap_iv_, &ctext)) != ZX_OK) {
        return rc;
    }
    memcpy(block_.get() + off, ctext.get(), ctext.len());

    return ZX_OK;
}

zx_status_t Volume::Open(const crypto::Bytes& key, slot_num_t slot) {
    zx_status_t rc;

    for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
        if ((rc = Read()) != ZX_OK) {
            xprintf("failed to read block at %" PRIu64 ": %s\n", offset_, zx_status_get_string(rc));
        } else if ((rc = OpenBlock(key, slot)) != ZX_OK) {
            xprintf("failed to open block at %" PRIu64 ": %s\n", offset_, zx_status_get_string(rc));
        } else {
            return CommitBlock();
        }
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Volume::OpenBlock(const crypto::Bytes& key, slot_num_t slot) {
    zx_status_t rc;

    // Check the type GUID matches |kTypeGuid|.
    uint8_t* in = block_.get();
    if (memcmp(in, kTypeGuid, GUID_LEN) != 0) {
        xprintf("not a zxcrypt device\n");
        ;
        return ZX_ERR_NOT_SUPPORTED;
    }
    in += GUID_LEN;

    // Save the instance GUID
    if ((rc = guid_.Copy(in, GUID_LEN)) != ZX_OK) {
        return rc;
    }
    in += GUID_LEN;

    // Read the version
    uint32_t version;
    memcpy(&version, in, sizeof(version));
    in += sizeof(version);
    if ((rc != Configure(Version(ntohl(version)))) != ZX_OK ||
        (rc != DeriveSlotKeys(key, slot)) != ZX_OK) {
        return rc;
    }

    // Read in the data
    crypto::AEAD aead;
    crypto::Bytes ptext, ctext;
    zx_off_t off = kHeaderLen + (slot_len_ * slot);
    if ((rc = ctext.Copy(block_.get() + off, slot_len_)) != ZX_OK ||
        (rc = aead.InitOpen(aead_, wrap_key_)) != ZX_OK ||
        (rc = header_.Copy(block_.get(), kHeaderLen)) != ZX_OK ||
        (rc = aead.SetAD(header_)) != ZX_OK || (rc = aead.Open(wrap_iv_, ctext, &ptext)) != ZX_OK ||
        (rc = ptext.Split(&data_iv_)) != ZX_OK || (rc = ptext.Split(&data_key_)) != ZX_OK) {
        return rc;
    }
    if (ptext.len() != 0) {
        xprintf("%zu unused bytes\n", ptext.len());
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

// Device methods

zx_status_t Volume::Ioctl(int op, const void* in, size_t in_len, void* out, size_t out_len) {
    zx_status_t rc;
    // Don't include debug messages here; some errors (e.g. ZX_ERR_NOT_SUPPORTED)
    // are expected under certain conditions (e.g. calling FVM ioctls on a non-FVM
    // device).  Handle error reporting at the call sites instead.
    if (dev_) {
        size_t actual;
        if ((rc = device_ioctl(dev_, op, in, in_len, out, out_len, &actual)) < 0) {
            return rc;
        }
    } else {
        ssize_t res;
        if ((res = fdio_ioctl(fd_.get(), op, in, in_len, out, out_len)) < 0) {
            return static_cast<zx_status_t>(res);
        }
    }
    return ZX_OK;
}

zx_status_t Volume::Read() {
    if (dev_) {
        return SyncIO(dev_, BLOCK_OP_READ, block_.get(), offset_, block_.len());
    } else {
        if (lseek(fd_.get(), offset_, SEEK_SET) < 0) {
            xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", fd_.get(), offset_,
                    strerror(errno));
            return ZX_ERR_IO;
        }
        ssize_t res;
        if ((res = read(fd_.get(), block_.get(), block_.len())) < 0) {
            xprintf("read(%d, %p, %zu) failed: %s\n", fd_.get(), block_.get(), block_.len(),
                    strerror(errno));
            return ZX_ERR_IO;
        }
        if (static_cast<size_t>(res) != block_.len()) {
            xprintf("short read: have %zd, need %zu\n", res, block_.len());
            return ZX_ERR_IO;
        }
        return ZX_OK;
    }
}

zx_status_t Volume::Write() {
    if (dev_) {
        return SyncIO(dev_, BLOCK_OP_WRITE, block_.get(), offset_, block_.len());
    } else {
        if (lseek(fd_.get(), offset_, SEEK_SET) < 0) {
            xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", fd_.get(), offset_,
                    strerror(errno));
            return ZX_ERR_IO;
        }
        ssize_t res;
        if ((res = write(fd_.get(), block_.get(), block_.len())) < 0) {
            xprintf("write(%d, %p, %zu) failed: %s\n", fd_.get(), block_.get(), block_.len(),
                    strerror(errno));
            return ZX_ERR_IO;
        }
        if (static_cast<size_t>(res) != block_.len()) {
            xprintf("short read: have %zd, need %zu\n", res, block_.len());
            return ZX_ERR_IO;
        }
        return ZX_OK;
    }
}

} // namespace zxcrypt
