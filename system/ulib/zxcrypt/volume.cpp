// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/hkdf.h>
#include <crypto/secret.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/debug.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <lib/zx/vmo.h>
#include <sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>
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
const Volume::Version Volume::kDefaultVersion = Volume::kAES256_XTS_SHA256;

// The amount of data that can "in-flight" to the underlying block device before the zxcrypt
// driver begins queuing transactions
//
// TODO(aarongreen): See ZX-1616.  Tune this value.  Possibly break into several smaller VMOs if we
// want to allow some to be recycled; support for this doesn't currently exist. Up to 64 MB may be
// in flight at once.  The device's max_transfer_size will be capped at 1/4 of this value.
const uint32_t Volume::kBufferSize = 1U << 24;
static_assert(Volume::kBufferSize % PAGE_SIZE == 0, "kBufferSize must be page aligned");

namespace {

// The zxcrypt driver
const char* kDriverLib = "/boot/driver/zxcrypt.so";

// The number of metadata blocks in a reserved metadata slice, each holding a copy of the
// superblock.
const size_t kMetadataBlocks = 2;

// HKDF labels
const size_t kMaxLabelLen = 16;
const char* kWrapKeyLabel = "wrap key %" PRIu64;
const char* kWrapIvLabel = "wrap iv %" PRIu64;

// Header is type GUID | instance GUID | version.
const size_t kHeaderLen = sizeof(zxcrypt_magic) + GUID_LEN + sizeof(uint32_t);

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

zx_status_t Volume::Create(fbl::unique_fd fd, const crypto::Secret& key,
                           fbl::unique_ptr<Volume>* out) {
    zx_status_t rc;

    if (!fd) {
        xprintf("bad parameter(s): fd=%d\n", fd.get());
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Volume> volume(new (&ac) Volume(fbl::move(fd)));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Volume));
        return ZX_ERR_NO_MEMORY;
    }

    if ((rc = volume->Init()) != ZX_OK || (rc = volume->CreateBlock()) != ZX_OK ||
        (rc = volume->SealBlock(key, 0)) != ZX_OK || (rc = volume->CommitBlock()) != ZX_OK) {
        return rc;
    }

    if (out) {
        *out = fbl::move(volume);
    }
    return ZX_OK;
}

zx_status_t Volume::Unlock(fbl::unique_fd fd, const crypto::Secret& key, key_slot_t slot,
                           fbl::unique_ptr<Volume>* out) {
    zx_status_t rc;

    if (!fd || !out) {
        xprintf("bad parameter(s): fd=%d, out=%p\n", fd.get(), out);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Volume> volume(new (&ac) Volume(fbl::move(fd)));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Volume));
        return ZX_ERR_NO_MEMORY;
    }
    if ((rc = volume->Init()) != ZX_OK || (rc = volume->Unseal(key, slot)) != ZX_OK) {
        return rc;
    }

    *out = fbl::move(volume);
    return ZX_OK;
}

zx_status_t Volume::Open(const zx::duration& timeout, fbl::unique_fd* out) {
    zx_status_t rc;
    ssize_t res;

    // Get the full device path
    char base[PATH_MAX/2];
    char path[PATH_MAX/2];
    if ((res = ioctl_device_get_topo_path(fd_.get(), base, sizeof(base))) < 0) {
        rc = static_cast<zx_status_t>(res);
        xprintf("could not find parent device: %s\n", zx_status_get_string(rc));
        return rc;
    }
    snprintf(path, sizeof(path), "%s/zxcrypt/block", base);

    // Early return if already bound
    fbl::unique_fd fd(open(path, O_RDWR));
    if (fd) {
        out->reset(fd.release());
        return ZX_OK;
    }

    // Bind the device
    if ((res = ioctl_device_bind(fd_.get(), kDriverLib, strlen(kDriverLib))) < 0) {
        rc = static_cast<zx_status_t>(res);
        xprintf("could not bind zxcrypt driver: %s\n", zx_status_get_string(rc));
        return rc;
    }
    if ((rc = wait_for_device(path, timeout.get())) != ZX_OK) {
        xprintf("zxcrypt driver failed to bind: %s\n", zx_status_get_string(rc));
        return rc;
    }
    fd.reset(open(path, O_RDWR));
    if (!fd) {
        xprintf("failed to open zxcrypt volume\n");
        return ZX_ERR_NOT_FOUND;
    }

    out->reset(fd.release());
    return ZX_OK;
}

zx_status_t Volume::Enroll(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!dev_); // Cannot enroll from driver

    if (!block_.get()) {
        xprintf("not initialized\n");
        return ZX_ERR_BAD_STATE;
    }
    if (slot >= num_key_slots_) {
        xprintf("bad parameter(s): slot=%" PRIu64 "\n", slot);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((rc = SealBlock(key, slot)) != ZX_OK || (rc = CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Volume::Revoke(key_slot_t slot) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!dev_); // Cannot revoke from driver

    if (!block_.get()) {
        xprintf("not initialized\n");
        return ZX_ERR_BAD_STATE;
    }
    if (slot >= num_key_slots_) {
        xprintf("bad parameter(s): slot=%" PRIu64 "\n", slot);
        return ZX_ERR_INVALID_ARGS;
    }
    zx_off_t off = kHeaderLen + (slot_len_ * slot);
    crypto::Bytes invalid;
    if ((rc = invalid.Randomize(slot_len_)) != ZX_OK ||
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

zx_status_t Volume::Unlock(zx_device_t* dev, const crypto::Secret& key, key_slot_t slot,
                           fbl::unique_ptr<Volume>* out) {
    zx_status_t rc;

    if (!dev || !out) {
        xprintf("bad parameter(s): dev=%p, out=%p\n", dev, out);
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AllocChecker ac;
    fbl::unique_ptr<Volume> volume(new (&ac) Volume(dev));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Volume));
        return ZX_ERR_NO_MEMORY;
    }
    if ((rc = volume->Init()) != ZX_OK || (rc = volume->Unseal(key, slot)) != ZX_OK) {
        return rc;
    }

    *out = fbl::move(volume);
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
        return ZX_ERR_BAD_STATE;
    }
    if ((rc = cipher->Init(cipher_, direction, data_key_, data_iv_, block_.len())) != ZX_OK) {
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
    block_info_t blk;
    if ((rc = Ioctl(IOCTL_BLOCK_GET_INFO, nullptr, 0, &blk, sizeof(blk))) < 0) {
        xprintf("failed to get block info: %s\n", zx_status_get_string(rc));
        return rc;
    }
    // Check that we meet the minimum size.
    if (blk.block_count < kMetadataBlocks) {
        xprintf("device is too small; have %" PRIu64 " blocks, need %" PRIu64 "\n", blk.block_count,
                kMetadataBlocks);
        return ZX_ERR_NOT_SUPPORTED;
    }
    reserved_blocks_ = kMetadataBlocks;
    // Allocate block buffer
    if ((rc = block_.Resize(blk.block_size)) != ZX_OK) {
        return rc;
    }
    // Get FVM info
    fvm_info_t fvm;
    switch ((rc = Ioctl(IOCTL_BLOCK_FVM_QUERY, nullptr, 0, &fvm, sizeof(fvm)))) {
    case ZX_OK: {
        // This *IS* an FVM partition.
        // Ensure first kReservedSlices + 1 slices are allocated
        size_t blocks_per_slice = fvm.slice_size / blk.block_size;
        reserved_blocks_ = fbl::round_up(reserved_blocks_, blocks_per_slice);
        reserved_slices_ = reserved_blocks_ / blocks_per_slice;
        size_t required = reserved_slices_ + 1;
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
        break;
    }
    case ZX_ERR_NOT_SUPPORTED:
        // This is *NOT* an FVM partition.
        break;
    default:
        // An error occurred
        return rc;
    }

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

    default:
        xprintf("unknown version: %u\n", version);
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t key_len, iv_len, tag_len;
    if ((rc = crypto::Cipher::GetKeyLen(cipher_, &key_len)) != ZX_OK ||
        (rc = crypto::Cipher::GetIVLen(cipher_, &iv_len)) != ZX_OK ||
        (rc = crypto::AEAD::GetTagLen(aead_, &tag_len)) != ZX_OK ||
        (rc = crypto::digest::GetDigestLen(digest_, &digest_len_)) != ZX_OK) {
        return rc;
    }

    slot_len_ = key_len + iv_len + tag_len;
    num_key_slots_ = (block_.len() - kHeaderLen) / slot_len_;
    if (num_key_slots_ == 0) {
        xprintf("block size is too small; have %zu, need %zu\n", block_.len(),
                kHeaderLen + slot_len_);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_OK;
}

zx_status_t Volume::DeriveSlotKeys(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;

    crypto::HKDF hkdf;
    char label[kMaxLabelLen];
    if ((rc = hkdf.Init(digest_, key, guid_)) != ZX_OK) {
        return rc;
    }
    snprintf(label, kMaxLabelLen, kWrapKeyLabel, slot);
    size_t len;
    if ((rc = crypto::AEAD::GetKeyLen(aead_, &len)) != ZX_OK ||
        (rc = hkdf.Derive(label, len, &wrap_key_)) != ZX_OK) {
        xprintf("failed to derive wrap key: %s\n", zx_status_get_string(rc));
        return rc;
    }
    snprintf(label, kMaxLabelLen, kWrapIvLabel, slot);
    crypto::Secret wrap_iv;
    if ((rc = crypto::AEAD::GetIVLen(aead_, &len)) != ZX_OK ||
        (rc = hkdf.Derive(label, len, &wrap_iv_)) != ZX_OK) {
        xprintf("failed to derive wrap IV: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

void Volume::Reset() {
    block_.Resize(0);
    offset_ = UINT64_MAX;
    aead_ = crypto::AEAD::kUninitialized;
    wrap_key_.Clear();
    cipher_ = crypto::Cipher::kUninitialized;
    data_key_.Clear();
    slot_len_ = 0;
    num_key_slots_ = 0;
    digest_ = crypto::digest::kUninitialized;
}

// Block methods

zx_status_t Volume::Begin() {
    offset_ = 0;
    return ZX_ERR_NEXT;
}

zx_status_t Volume::Next() {
    offset_ += block_.len();
    return (offset_ / block_.len()) < kMetadataBlocks ? ZX_ERR_NEXT : ZX_ERR_STOP;
}

zx_status_t Volume::CreateBlock() {
    zx_status_t rc;

    // Create a "backdrop" of random data
    if ((rc = block_.Randomize()) != ZX_OK) {
        return rc;
    }

    // Write the variant 1/version 1 type GUID according to RFC 4122.
    // TODO(aarongreen): ZX-2106.  This and other magic numbers should be moved to a public/zircon
    // header, and the dependency removed.
    uint8_t* out = block_.get();
    memcpy(out, zxcrypt_magic, sizeof(zxcrypt_magic));
    out += sizeof(zxcrypt_magic);

    // Create a variant 1/version 4 instance GUID according to RFC 4122.
    if ((rc = guid_.Randomize(GUID_LEN)) != ZX_OK) {
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
    size_t key_len, iv_len;
    if ((rc = crypto::Cipher::GetKeyLen(cipher_, &key_len)) != ZX_OK ||
        (rc = crypto::Cipher::GetIVLen(cipher_, &iv_len)) != ZX_OK ||
        (rc = data_key_.Generate(key_len)) != ZX_OK ||
        (rc = data_iv_.Resize(iv_len)) != ZX_OK ||
        (rc = data_iv_.Randomize()) != ZX_OK ||
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

zx_status_t Volume::SealBlock(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;

    if (slot >= num_key_slots_) {
        xprintf("bad key slot: %" PRIu64 "\n", slot);
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Encrypt the data key
    zx_off_t nonce;
    crypto::AEAD aead;
    crypto::Bytes ptext, ctext;
    zx_off_t off = kHeaderLen + (slot_len_ * slot);
    zx_off_t data_key_off = 0;
    zx_off_t data_iv_off = data_key_.len();
    if ((rc = ptext.Copy(data_key_.get(), data_key_.len(), data_key_off)) != ZX_OK ||
        (rc = ptext.Copy(data_iv_.get(), data_iv_.len(), data_iv_off)) != ZX_OK ||
        (rc = DeriveSlotKeys(key, slot)) != ZX_OK ||
        (rc = aead.InitSeal(aead_, wrap_key_, wrap_iv_)) != ZX_OK ||
        (rc = aead.Seal(ptext, header_, &nonce, &ctext)) != ZX_OK) {
        return rc;
    }
    // Check that we'll be able to unseal.
    if (memcmp(&nonce, wrap_iv_.get(), sizeof(nonce)) != 0) {
        xprintf("unexpected nonce: %" PRIu64 "\n", nonce);
        return ZX_ERR_INTERNAL;
    }

    memcpy(block_.get() + off, ctext.get(), ctext.len());
    return ZX_OK;
}

zx_status_t Volume::Unseal(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;

    for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
        if ((rc = Read()) != ZX_OK) {
            xprintf("failed to read block at %" PRIu64 ": %s\n", offset_, zx_status_get_string(rc));
        } else if ((rc = UnsealBlock(key, slot)) != ZX_OK) {
            xprintf("failed to open block at %" PRIu64 ": %s\n", offset_, zx_status_get_string(rc));
        } else {
            return CommitBlock();
        }
    }

    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Volume::UnsealBlock(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;

    // Check the type GUID matches |kTypeGuid|.
    uint8_t* in = block_.get();
    if (memcmp(in, zxcrypt_magic, sizeof(zxcrypt_magic)) != 0) {
        xprintf("not a zxcrypt device\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    in += sizeof(zxcrypt_magic);

    // Save the instance GUID
    if ((rc = guid_.Copy(in, GUID_LEN)) != ZX_OK) {
        return rc;
    }
    in += GUID_LEN;

    // Read the version
    uint32_t version;
    memcpy(&version, in, sizeof(version));
    in += sizeof(version);
    if ((rc != Configure(Version(ntohl(version)))) != ZX_OK) {
        return rc;
    }
    if (slot >= num_key_slots_) {
        xprintf("bad key slot: %" PRIu64 "\n", slot);
        return ZX_ERR_OUT_OF_RANGE;
    }
    if ((rc != DeriveSlotKeys(key, slot)) != ZX_OK) {
        return rc;
    }

    // Extract nonce from IV.
    zx_off_t nonce;
    memcpy(&nonce, wrap_iv_.get(), sizeof(nonce));

    // Read in the data
    crypto::AEAD aead;
    crypto::Bytes ptext, ctext, data_key;
    zx_off_t off = kHeaderLen + (slot_len_ * slot);

    size_t key_off, key_len, iv_off, iv_len;
    uint8_t *key_buf;
    if ((rc = crypto::Cipher::GetKeyLen(cipher_, &key_len)) != ZX_OK ||
        (rc = crypto::Cipher::GetIVLen(cipher_, &iv_len)) != ZX_OK ||
        (rc = data_key_.Allocate(key_len, &key_buf)) != ZX_OK) {
        return rc;
    }

    key_off = 0;
    iv_off = data_key_.len();
    if ((rc = ctext.Copy(block_.get() + off, slot_len_)) != ZX_OK ||
        (rc = aead.InitOpen(aead_, wrap_key_, wrap_iv_)) != ZX_OK ||
        (rc = header_.Copy(block_.get(), kHeaderLen)) != ZX_OK ||
        (rc = aead.Open(nonce, ctext, header_, &ptext)) != ZX_OK ||
        (rc = data_iv_.Copy(ptext.get() + iv_off, iv_len)) != ZX_OK) {
        return rc;
    }
    memcpy(key_buf, ptext.get() + key_off, key_len);

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
    zx_status_t rc;

    if (dev_) {
        if ((rc = SyncIO(dev_, BLOCK_OP_READ, block_.get(), offset_, block_.len())) != ZX_OK) {
            return rc;
        }
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
    }

    return ZX_OK;
}

zx_status_t Volume::Write() {
    zx_status_t rc;

    if (dev_) {
        if ((rc = SyncIO(dev_, BLOCK_OP_WRITE, block_.get(), offset_, block_.len())) != ZX_OK) {
            return rc;
        }
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
    }
    return ZX_OK;
}

} // namespace zxcrypt
