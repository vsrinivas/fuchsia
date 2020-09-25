// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <lib/zircon-internal/debug.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/hkdf.h>
#include <crypto/secret.h>
#include <fbl/algorithm.h>
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
// TODO(aarongreen): See fxbug.dev/31498.  Tune this value.  Possibly break into several smaller VMOs if we
// want to allow some to be recycled; support for this doesn't currently exist. Up to 64 MB may be
// in flight at once.  The device's max_transfer_size will be capped at 1/4 of this value.
__EXPORT
const uint32_t Volume::kBufferSize = 1U << 24;
static_assert(Volume::kBufferSize % PAGE_SIZE == 0, "kBufferSize must be page aligned");

namespace {

// The number of metadata blocks in a reserved metadata slice, each holding a copy of the
// superblock.
const size_t kMetadataBlocks = 2;

// HKDF labels
const size_t kMaxLabelLen = 16;
const char* kWrapKeyLabel = "wrap key %" PRIu64;
const char* kWrapIvLabel = "wrap iv %" PRIu64;

// Header is type GUID | instance GUID | version.
const size_t kHeaderLen = sizeof(zxcrypt_magic) + BLOCK_GUID_LEN + sizeof(uint32_t);

}  // namespace

Volume::Volume() { Reset(); }

Volume::~Volume() {}

void Volume::Reset() {
  reserved_blocks_ = 0;
  reserved_slices_ = 0;
  block_.Resize(0);
  offset_ = UINT64_MAX;
  guid_.Resize(0);
  header_.Resize(0);
  aead_ = crypto::AEAD::kUninitialized;
  wrap_key_.Clear();
  wrap_iv_.Resize(0);
  cipher_ = crypto::Cipher::kUninitialized;
  data_key_.Clear();
  data_iv_.Resize(0);
  slot_len_ = 0;
  num_key_slots_ = 0;
  digest_ = crypto::digest::kUninitialized;
}

zx_status_t Volume::Unlock(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
    if ((rc = Read()) != ZX_OK) {
      xprintf("failed to read block at %" PRIu64 ": %d\n", offset_, rc);
    } else if ((rc = UnsealBlock(key, slot)) != ZX_OK) {
      xprintf("failed to open block at %" PRIu64 ": %d\n", offset_, rc);
    } else {
      return ZX_OK;
    }
  }

  return ZX_ERR_ACCESS_DENIED;
}

zx_status_t Volume::Shred() {
  zx_status_t rc;

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

zx_status_t Volume::GetSlotOffset(key_slot_t slot, zx_off_t* out) const {
  if (!block_.get()) {
    xprintf("not initialized\n");
    return ZX_ERR_BAD_STATE;
  }

  zx_off_t off;
  if (mul_overflow(slot, slot_len_, &off) || add_overflow(kHeaderLen, off, &off) ||
      off > block_.len() - slot_len_) {
    xprintf("bad key slot: %" PRIu64 "\n", slot);
    return ZX_ERR_INVALID_ARGS;
  }

  if (out) {
    *out = off;
  }
  return ZX_OK;
}

zx_status_t Volume::Init() {
  zx_status_t rc;

  // Get block info; align our blocks to pages
  BlockInfo blk;
  if ((rc = GetBlockInfo(&blk)) != ZX_OK) {
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
  uint64_t fvm_slice_size;
  switch ((rc = GetFvmSliceSize(&fvm_slice_size))) {
    case ZX_OK: {
      // This *IS* an FVM partition.
      // Ensure first kReservedSlices + 1 slices are allocated
      size_t blocks_per_slice = fvm_slice_size / blk.block_size;
      reserved_blocks_ = fbl::round_up(reserved_blocks_, blocks_per_slice);
      reserved_slices_ = reserved_blocks_ / blocks_per_slice;
      size_t required_slices = reserved_slices_ + 1;
      size_t contiguous_chunk_size = 0;

      // We're going to go through the vslice address space, ensuring that all
      // of the first `required_slices` slices are allocated.
      for (size_t slice_off = 0; slice_off < required_slices; slice_off += contiguous_chunk_size) {
        // Ask about the next contiguous range starting at `slice_off`.
        SliceRegion ranges[MAX_SLICE_REGIONS];
        uint64_t range_count;
        if ((rc = DoBlockFvmVsliceQuery(slice_off, ranges, &range_count)) != ZX_OK ||
            range_count == 0 || ((contiguous_chunk_size = ranges[0].count) == 0)) {
          xprintf("FVM Vslice Query failed: %s\n", zx_status_get_string(rc));
          return rc;
        }

        // If it's already allocated, continue to the next range.  The for loop
        // advances by `contiguous_chunk_size`
        if (ranges[0].allocated) {
          continue;
        };

        // Otherwise, allocate it -- either up to the end of the contiguous
        // unallocated chunk (if that still doesn't cover the number of slices
        // we need, in which case we'll keep looping) or just as many slices as
        // we require.
        uint64_t extend_start_slice = slice_off;
        uint64_t extend_length = std::min(required_slices - slice_off, contiguous_chunk_size);

        if ((rc = DoBlockFvmExtend(extend_start_slice, extend_length)) != ZX_OK) {
          xprintf("failed to extend FVM partition: %s\n", zx_status_get_string(rc));
          return rc;
        }
      }
      break;
    }
    case ZX_ERR_NOT_SUPPORTED:
      // This is *NOT* an FVM partition.
      xprintf("Not an FVM partition\n");
      break;
    default:
      xprintf("init failed: %s\n", zx_status_get_string(rc));
      // An error occurred
      return rc;
  }

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
      (rc = crypto::AEAD::GetTagLen(aead_, &tag_len)) != ZX_OK) {
    return rc;
  }

  slot_len_ = key_len + iv_len + tag_len;
  num_key_slots_ = (block_.len() - kHeaderLen) / slot_len_;
  if (num_key_slots_ == 0) {
    xprintf("block size is too small; have %zu, need %zu\n", block_.len(), kHeaderLen + slot_len_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t Volume::DeriveSlotKeys(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  crypto::HKDF hkdf;
  char label[kMaxLabelLen];

  // We tolerate 128-bit keys here because some hardware we wish to operate on
  // only has 128-bits of random keys in hardware.  We believe that this
  // entropy is sufficient for our purposes.
  size_t key_len = key.len();
  if (key_len == 16) {
    rc = hkdf.Init(digest_, key, guid_, crypto::HKDF::ALLOW_WEAK_KEY);
  } else if (key_len == 32) {
    rc = hkdf.Init(digest_, key, guid_);
  } else {
    xprintf("invalid key length %lu (acceptable values are 16, 32)\n", key_len);
    return ZX_ERR_INVALID_ARGS;
  }
  if (rc != ZX_OK) {
    xprintf("hkdf.Init failed %s\n", zx_status_get_string(rc));
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
  uint8_t* out = block_.get();
  memcpy(out, zxcrypt_magic, sizeof(zxcrypt_magic));
  out += sizeof(zxcrypt_magic);

  // Create a variant 1/version 4 instance GUID according to RFC 4122.
  if ((rc = guid_.Randomize(BLOCK_GUID_LEN)) != ZX_OK) {
    return rc;
  }
  guid_[6] = (guid_[6] & 0x0F) | 0x40;
  guid_[8] = (guid_[8] & 0x3F) | 0x80;
  memcpy(out, guid_.get(), BLOCK_GUID_LEN);
  out += BLOCK_GUID_LEN;

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
      (rc = data_key_.Generate(key_len)) != ZX_OK || (rc = data_iv_.Resize(iv_len)) != ZX_OK ||
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
    xprintf("zxcrypt: Cannot copy block: %s\n", zx_status_get_string(rc));
    return rc;
  }
  for (rc = Begin(); rc == ZX_ERR_NEXT; rc = Next()) {
    if ((rc = Read()) != ZX_OK) {
      xprintf("zxcrypt: CommitBlock Read failed: %s\n", zx_status_get_string(rc));
      return rc;
    }
    // Only write back blocks that don't match.
    if (block_ == block) {
      continue;
    }
    if ((rc = block_.Copy(block)) != ZX_OK || (rc = Write()) != ZX_OK) {
      xprintf("zxcrypt: CommitBlock Write failed for offset %" PRIu64 ": %s\n", offset_,
              zx_status_get_string(rc));
    }
  }
  return ZX_OK;
}

zx_status_t Volume::SealBlock(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  // Encrypt the data key
  zx_off_t nonce;
  crypto::AEAD aead;
  crypto::Bytes ptext, ctext;
  zx_off_t off;
  zx_off_t data_key_off = 0;
  zx_off_t data_iv_off = data_key_.len();
  if ((rc = GetSlotOffset(slot, &off)) != ZX_OK) {
    xprintf("GetSlotOffset for slot %" PRIu64 " failed: %s\n", slot, zx_status_get_string(rc));
    return rc;
  }
  if ((rc = ptext.Copy(data_key_.get(), data_key_.len(), data_key_off)) != ZX_OK) {
    xprintf("ptext.Copy (key) failed: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = ptext.Copy(data_iv_.get(), data_iv_.len(), data_iv_off)) != ZX_OK) {
    xprintf("ptext.Copy (iv) failed: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = DeriveSlotKeys(key, slot)) != ZX_OK) {
    xprintf("DeriveSlotKeys failed: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = aead.InitSeal(aead_, wrap_key_, wrap_iv_)) != ZX_OK) {
    xprintf("aead.InitSeal failed: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = aead.Seal(ptext, header_, &nonce, &ctext)) != ZX_OK) {
    xprintf("aead.Seal failed: %s", zx_status_get_string(rc));
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

zx_status_t Volume::UnsealBlock(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  // Check the type GUID matches |kTypeGuid|.
  const uint8_t* in = block_.get();
  if (memcmp(in, zxcrypt_magic, sizeof(zxcrypt_magic)) != 0) {
    xprintf("not a zxcrypt device\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  in += sizeof(zxcrypt_magic);

  // Save the instance GUID
  if ((rc = guid_.Copy(in, BLOCK_GUID_LEN)) != ZX_OK) {
    return rc;
  }
  in += BLOCK_GUID_LEN;

  // Read the version
  uint32_t version;
  memcpy(&version, in, sizeof(version));
  in += sizeof(version);

  // Read in the data
  zx_off_t off;
  size_t key_len, iv_len;
  uint8_t* key_buf;
  crypto::AEAD aead;
  crypto::Bytes ctext, ptext;
  if ((rc = Configure(Version(ntohl(version)))) != ZX_OK) {
    xprintf("Configure failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = GetSlotOffset(slot, &off)) != ZX_OK) {
    xprintf("GetSlotOffset failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = DeriveSlotKeys(key, slot)) != ZX_OK) {
    xprintf("DeriveSlotKeys failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = crypto::Cipher::GetKeyLen(cipher_, &key_len)) != ZX_OK) {
    xprintf("Cipher::GetKeyLen failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = crypto::Cipher::GetIVLen(cipher_, &iv_len)) != ZX_OK) {
    xprintf("Cipher::GetIVLen failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = data_key_.Allocate(key_len, &key_buf)) != ZX_OK) {
    xprintf("Secret::Allocate failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = ctext.Copy(block_.get() + off, slot_len_)) != ZX_OK) {
    xprintf("ctext.Copy failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = aead.InitOpen(aead_, wrap_key_, wrap_iv_)) != ZX_OK) {
    xprintf("aead.InitOpen failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = header_.Copy(block_.get(), kHeaderLen)) != ZX_OK) {
    xprintf("header_.Copy failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Extract nonce from IV.
  zx_off_t nonce;
  memcpy(&nonce, wrap_iv_.get(), sizeof(nonce));
  if ((rc = aead.Open(nonce, ctext, header_, &ptext)) != ZX_OK) {
    xprintf("aead.Open failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = data_iv_.Copy(ptext.get() + key_len, iv_len)) != ZX_OK) {
    xprintf("data_iv_.Copy failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  memcpy(key_buf, ptext.get(), key_len);

  return ZX_OK;
}

}  // namespace zxcrypt
