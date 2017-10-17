// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <crypto/aead.h>
#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/digest.h>
#include <ddk/device.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

// |zxcrypt::Superblock| manages the interactions of both driver and library code with the metadata
// used to format and operate zxcrypt devices.  Driver code uses the public constructor and instance
// methods, while library code can use the static methods with a file descriptor to the underlying
// block device.  The superblock is saved multiple times on disk to provide redundancy.
//
// It manages four types of key material:
//  - Root:, Provided by the consumers of this class.
//  - Data: Randomly generated at volume creation and used to encrypt and decrypt the volumes data.
//  - Wrap: Derived from the root keys and used to encrypt and decrypt the data key material.
//  - HMAC: Derived from the root keys and used to verify the integrity of the superblock.
namespace zxcrypt {

// An RFC 4122 variant 1/version 1 UUID.  It corresponds to Oct 17 12:00:00 PDT 2017.  The clock
// sequence and node address contain 'zxcrypt'.
const uint8_t kTypeGuid[GUID_LEN] = {0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7,
                                     0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74};

// TODO(aarongreen): ZX-1130 workaround: Until we have a means to pass the root key on binding, we
// simply use a null key of a fixed length. Remove this constant when ZX-1130 is resolved.
const size_t kZx1130KeyLen = 32;

using slot_num_t = zx_off_t;

class Superblock final {
public:
    // The supported version, named by the algorithms they use.  New version should increment the
    // version number and update the default version.  Zero indicates an error state.
    enum Version : uint32_t {
        kUninitialized = 0,
        kAES256_XTS_SHA256 = 1,
    };

    // The default version, used when sealing a new superblock.
    static const Version kDefaultVersion;

    // The number of key slots available for a single zxcrypt volume.
    static const slot_num_t kNumSlots;

    // The number of pairs of blocks reserved for superblock metadata.
    static const size_t kReservedPairs;

    ~Superblock();

    ////////////////
    // Library methods

    // Creates a new zxcrypt superblock associated with the given file descriptor, |fd|.  This will
    // format the block device as zxcrypt using the given |key|.  This method takes ownership of
    // |fd|.
    static zx_status_t Create(fbl::unique_fd fd, const crypto::Bytes& root_key);

    // Opens a zxcrypt superblock on the block device described by |fd| using the |key|
    // corresponding to given key |slot|.  The |fd| parameter means this factory method can be used
    // from libzxcrypt.  This method takes ownership of |fd|.
    static zx_status_t Open(fbl::unique_fd fd, const crypto::Bytes& key, slot_num_t slot,
                            fbl::unique_ptr<Superblock>* out);

    // Adds a given |root_key| using the given key |slot|.  This key can then be used to |Open| the
    // zxcrypt device.  This method can only be called if the superblock belongs to libzxcrypt.
    zx_status_t Enroll(const crypto::Bytes& root_key, slot_num_t slot);

    // Removes the root key in the given key |slot|.  This key can no longer be used to |Open| the
    // zxcrypt device.  This method can only be called if the superblock belongs to libzxcrypt.
    zx_status_t Revoke(slot_num_t slot);

    // Removes ALL keys, rendering any data in the zxcrypt device inaccessible.  It is an error to
    // call any method except the destructor on this instance after this methods returns.
    zx_status_t Shred();

    ////////////////
    // Driver methods

    // Opens a zxcrypt superblock on the block device described by |dev| using the |key|
    // corresponding to given key |slot|.  The |dev| parameter means this factory method can be used
    // from the zxcrypt driver.
    static zx_status_t Open(zx_device_t* dev, const crypto::Bytes& key, slot_num_t slot,
                            fbl::unique_ptr<Superblock>* out);

    // Copies the block device and FVM information.  If the parent device is not an FVM partition,
    // the FVM information is synthetically generated.  The parent device's FVM support can be
    // determined using |HasFVM|.
    zx_status_t GetInfo(block_info_t* out_blk, fvm_info_t* out_fvm);

    // Indicates if the underlying block device is an FVM partition.
    bool HasFVM() const { return has_fvm_; }

    // Returns the data key and IV in |out_key| and |out_iv|, respectively.  These can be used to
    // initialize a data cipher.  This method can only be called if the superblock belongs to the
    // zxcrypt driver.
    zx_status_t BindCiphers(crypto::Cipher* out_encrypt, crypto::Cipher* out_decrypt);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Superblock);

    // Use the static factory methods above instead of these constructors.
    explicit Superblock(fbl::unique_fd&& fd);
    explicit Superblock(zx_device_t* dev);

    ////////////////
    // Configuration methods

    // Retrieves the block and FVM information and adjusts it.
    zx_status_t Init();

    // Maps the superblock version to crypto algorithms.
    zx_status_t Configure(Version version);

    // Derives intermediate keys for the given key |slot| from the given |key|.
    zx_status_t DeriveSlotKeys(const crypto::Bytes& key, slot_num_t slot);

    // Resets all fields in this object to initial values
    void Reset();

    ////////////////
    // Block methods

    // Resets the superblock to the first location (block 0).
    zx_status_t Begin();

    // Advances the offset to the next superblock location.  Returns ZX_ERR_STOP if no more offsets
    // available; ZX_ERR_NEXT otherwise.
    zx_status_t Next();

    // Creates a new superblock, with a new instance GUID and data key and IV, and seals it with the
    // given |key|
    zx_status_t CreateBlock();

    // Writes |block| out to each of the superblock locations.
    zx_status_t CommitBlock();

    // Encrypts the current data key and IV to the given |slot| using the given |key|.
    zx_status_t SealBlock(const crypto::Bytes& key, slot_num_t slot);

    // Attempts to unseal the device using the given |key| corresponding to the given |slot| and any
    // superblock from the disk.
    zx_status_t Open(const crypto::Bytes& key, slot_num_t slot);

    // Reads the block and parses and checks various fields before attempting to open it with the
    // given |key| corresponding to the given |slot|.
    zx_status_t OpenBlock(const crypto::Bytes& key, slot_num_t slot);

    ////////////////
    // Device methods

    // Sends an I/O control message to the underlying device and reads the response.
    zx_status_t Ioctl(int op, const void* in, size_t in_len, void* out, size_t out_len);

    // Reads a block from the current offset on the underlying device.
    zx_status_t Read();

    // Writes a block to the current offset on the underlying device.
    zx_status_t Write();

    ////////////////
    // Fields

    // The underlying block device as accessed via DDK or FDIO.
    zx_device_t* dev_;
    fbl::unique_fd fd_;

    // The underlying device block and FVM information, and a flag to indicate whether the
    // underlying block device supports FVM ioctls.
    block_info_t blk_;
    fvm_info_t fvm_;
    bool has_fvm_;

    // Buffer holding the current block being examined, and its offset on the underlying device.
    crypto::Bytes block_;
    zx_off_t offset_;

    // The instance GUID for this device.
    crypto::Bytes guid_;
    // A copy of the entire header, used as AAD for the AEAD.
    crypto::Bytes header_;

    // The algorithm, lengths, and buffers for the key-wrapping AEAD.
    crypto::AEAD::Algorithm aead_;
    crypto::Bytes wrap_key_;
    crypto::Bytes wrap_iv_;

    // The algorithm for data processing Cipher and length of wrapped key material.
    crypto::Cipher::Algorithm cipher_;
    crypto::Bytes data_key_;
    crypto::Bytes data_iv_;
    size_t slot_len_;

    // The digest used by the HKDF.
    crypto::digest::Algorithm digest_;
    size_t digest_len_;
};

} // namespace zxcrypt
