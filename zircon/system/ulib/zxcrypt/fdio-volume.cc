// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/encrypted/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/fdio.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>
#include <kms-stateless/kms-stateless.h>
#include <ramdevice-client/ramdisk.h>  // Why does wait_for_device_at() come from here?
#include <zxcrypt/fdio-volume.h>
#include <zxcrypt/volume.h>

#define ZXDEBUG 0

namespace zxcrypt {

// The zxcrypt driver
const char* kDriverLib = "/boot/driver/zxcrypt.so";

namespace {

// Null key should be 32 bytes.
const size_t kKeyLength = 32;
const char kHardwareKeyInfo[] = "zxcrypt";

// How many bytes to read from /boot/config/zxcrypt?
const size_t kMaxKeySourcePolicyLength = 32;
const char kZxcryptConfigFile[] = "/boot/config/zxcrypt";

// Reads /boot/config/zxcrypt to determine what key source policy was selected for this product at
// build time.
//
// Returns ZX_OK and sets |out| to the appropriate KeySourcePolicy value if the file contents
// exactly match a known configuration value.
// Returns ZX_ERR_NOT_FOUND if the config file was not present
// Returns ZX_ERR_IO if the config file could not be read
// Returns ZX_ERR_BAD_STATE if the config value was not recognized.
zx_status_t SelectKeySourcePolicy(KeySourcePolicy* out) {
  fbl::unique_fd fd(open(kZxcryptConfigFile, O_RDONLY));
  if (!fd) {
    xprintf("zxcrypt: couldn't open %s\n", kZxcryptConfigFile);
    return ZX_ERR_NOT_FOUND;
  }

  char key_source_buf[kMaxKeySourcePolicyLength + 1];
  ssize_t len = read(fd.get(), key_source_buf, sizeof(key_source_buf) - 1);
  if (len < 0) {
    xprintf("zxcrypt: couldn't read %s\n", kZxcryptConfigFile);
    return ZX_ERR_IO;
  } else {
    // add null terminator
    key_source_buf[len] = '\0';
    // Dispatch if recognized
    if (strcmp(key_source_buf, "null") == 0) {
      *out = NullSource;
      return ZX_OK;
    }
    if (strcmp(key_source_buf, "tee") == 0) {
      *out = TeeRequiredSource;
      return ZX_OK;
    }
    if (strcmp(key_source_buf, "tee-transitional") == 0) {
      *out = TeeTransitionalSource;
      return ZX_OK;
    }
    if (strcmp(key_source_buf, "tee-opportunistic") == 0) {
      *out = TeeOpportunisticSource;
      return ZX_OK;
    }
    return ZX_ERR_BAD_STATE;
  }
}

}  // namespace

// Returns a ordered vector of |KeySource|s, representing all key sources,
// ordered from most-preferred to least-preferred, that we should try for the
// purposes of creating a new volume
__EXPORT
fbl::Vector<KeySource> ComputeEffectiveCreatePolicy(KeySourcePolicy ksp) {
  fbl::Vector<KeySource> r;
  switch (ksp) {
    case NullSource:
      r = {kNullSource};
      break;
    case TeeRequiredSource:
    case TeeTransitionalSource:
      r = {kTeeSource};
      break;
    case TeeOpportunisticSource:
      r = {kTeeSource, kNullSource};
      break;
  }
  return r;
}

// Returns a ordered vector of |KeySource|s, representing all key sources,
// ordered from most-preferred to least-preferred, that we should try for the
// purposes of unsealing an existing volume
__EXPORT
fbl::Vector<KeySource> ComputeEffectiveUnsealPolicy(KeySourcePolicy ksp) {
  fbl::Vector<KeySource> r;
  switch (ksp) {
    case NullSource:
      r = {kNullSource};
      break;
    case TeeRequiredSource:
      r = {kTeeSource};
      break;
    case TeeTransitionalSource:
    case TeeOpportunisticSource:
      r = {kTeeSource, kNullSource};
      break;
  }
  return r;
}

__EXPORT
zx_status_t TryWithKeysFrom(
    const fbl::Vector<KeySource>& ordered_key_sources, Activity activity,
    fbl::Function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)> callback) {
  zx_status_t rc = ZX_ERR_INTERNAL;
  for (auto& key_source : ordered_key_sources) {
    switch (key_source) {
      case kNullSource: {
        auto key_buf = std::unique_ptr<uint8_t[]>(new uint8_t[kKeyLength]);
        memset(key_buf.get(), 0, kKeyLength);
        rc = callback(std::move(key_buf), kKeyLength);
      } break;
      case kTeeSource: {
        // key info is |kHardwareKeyInfo| padded with 0.
        uint8_t key_info[kms_stateless::kExpectedKeyInfoSize] = {0};
        memcpy(key_info, kHardwareKeyInfo, sizeof(kHardwareKeyInfo));
        // make names for these so the callback to kms_stateless can
        // copy them out later
        std::unique_ptr<uint8_t[]> key_buf;
        size_t key_size;
        zx_status_t kms_rc = kms_stateless::GetHardwareDerivedKey(
            [&](std::unique_ptr<uint8_t[]> cb_key_buffer, size_t cb_key_size) {
              key_size = cb_key_size;
              key_buf = std::unique_ptr<uint8_t[]>(new uint8_t[cb_key_size]);
              memcpy(key_buf.get(), cb_key_buffer.get(), cb_key_size);
              return ZX_OK;
            },
            key_info);

        if (kms_rc != ZX_OK) {
          rc = kms_rc;
          break;
        }

        rc = callback(std::move(key_buf), key_size);
      } break;
    }
    if (rc == ZX_OK) {
      return rc;
    }
  }

  xprintf("TryWithKeysFrom (%s): none of the %lu key sources succeeded\n",
          activity == Activity::Create ? "create" : "unseal", ordered_key_sources.size());
  return rc;
}

FdioVolumeManager::FdioVolumeManager(zx::channel&& chan) : chan_(std::move(chan)) {}

zx_status_t FdioVolumeManager::Unseal(const uint8_t* key, size_t key_len, uint8_t slot) {
  zx_status_t rc;
  zx_status_t call_status;
  if ((rc = fuchsia_hardware_block_encrypted_DeviceManagerUnseal(chan_.get(),
                                                                 key, key_len,
                                                                 slot,
                                                                 &call_status)) != ZX_OK) {
    xprintf("failed to call Unseal: %s\n", zx_status_get_string(rc));
    return rc;
  }

  if (call_status != ZX_OK) {
    xprintf("failed to Unseal: %s\n", zx_status_get_string(call_status));
  }
  return call_status;
}

zx_status_t FdioVolumeManager::UnsealWithDeviceKey(uint8_t slot) {
  KeySourcePolicy source;
  zx_status_t rc;
  rc = SelectKeySourcePolicy(&source);
  if (rc != ZX_OK) {
    return rc;
  }

  auto ordered_key_sources = ComputeEffectiveUnsealPolicy(source);

  return TryWithKeysFrom(ordered_key_sources, Activity::Unseal,
                         [&](std::unique_ptr<uint8_t[]> key_buffer, size_t key_size) {
                           return Unseal(key_buffer.release(), key_size, slot);
                         });
}

zx_status_t FdioVolumeManager::Seal() {
  zx_status_t rc;
  zx_status_t call_status;
  if ((rc = fuchsia_hardware_block_encrypted_DeviceManagerSeal(chan_.get(),
                                                               &call_status)) != ZX_OK) {
    xprintf("failed to call Seal: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if (call_status != ZX_OK) {
    xprintf("failed to Seal: %s\n", zx_status_get_string(call_status));
  }
  return call_status;
}

zx_status_t FdioVolumeManager::Shred() {
  zx_status_t rc;
  zx_status_t call_status;
  if ((rc = fuchsia_hardware_block_encrypted_DeviceManagerShred(chan_.get(),
                                                                &call_status)) != ZX_OK) {
    xprintf("failed to call Shred: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if (call_status != ZX_OK) {
    xprintf("failed to Shred: %s\n", zx_status_get_string(call_status));
  }
  return call_status;
}

FdioVolume::FdioVolume(fbl::unique_fd&& block_dev_fd, fbl::unique_fd&& devfs_root_fd)
    : Volume(), block_dev_fd_(std::move(block_dev_fd)), devfs_root_fd_(std::move(devfs_root_fd)) {}

zx_status_t FdioVolume::Init(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                             std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  if (!block_dev_fd || !devfs_root_fd || !out) {
    xprintf("bad parameter(s): block_dev_fd=%d, devfs_root_fd=%d out=%p\n", block_dev_fd.get(),
            devfs_root_fd.get(), out);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<FdioVolume> volume(
      new (&ac) FdioVolume(std::move(block_dev_fd), std::move(devfs_root_fd)));
  if (!ac.check()) {
    xprintf("allocation failed: %zu bytes\n", sizeof(FdioVolume));
    return ZX_ERR_NO_MEMORY;
  }

  if ((rc = volume->Init()) != ZX_OK) {
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::Create(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                               const crypto::Secret& key, std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  std::unique_ptr<FdioVolume> volume;

  if ((rc = FdioVolume::Init(std::move(block_dev_fd), std::move(devfs_root_fd), &volume)) !=
      ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = volume->CreateBlock()) != ZX_OK) {
    xprintf("CreateBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = volume->SealBlock(key, 0)) != ZX_OK) {
    xprintf("SealBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = volume->CommitBlock()) != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  if (out) {
    *out = std::move(volume);
  }
  return ZX_OK;
}

zx_status_t FdioVolume::CreateWithDeviceKey(fbl::unique_fd&& block_dev_fd,
                                            fbl::unique_fd&& devfs_root_fd,
                                            std::unique_ptr<FdioVolume>* out) {
  KeySourcePolicy source;
  zx_status_t rc;
  rc = SelectKeySourcePolicy(&source);
  if (rc != ZX_OK) {
    return rc;
  }

  // Figure out which keying approaches we'll try, based on the key source
  // policy and context we're using this key in
  auto ordered_key_sources = ComputeEffectiveCreatePolicy(source);
  return TryWithKeysFrom(ordered_key_sources, Activity::Create,
                         [&](std::unique_ptr<uint8_t[]> key_buffer, size_t key_size) {
                           crypto::Secret secret;
                           zx_status_t rc;
                           uint8_t* inner;
                           rc = secret.Allocate(key_size, &inner);
                           if (rc != ZX_OK) {
                             xprintf("zxcrypt: couldn't allocate secret\n");
                             return rc;
                           }
                           memcpy(inner, key_buffer.get(), key_size);
                           rc = FdioVolume::Create(std::move(block_dev_fd),
                                                   std::move(devfs_root_fd), secret, out);
                           return rc;
                         });
}

zx_status_t FdioVolume::Unlock(fbl::unique_fd block_dev_fd, fbl::unique_fd devfs_root_fd,
                               const crypto::Secret& key, key_slot_t slot,
                               std::unique_ptr<FdioVolume>* out) {
  zx_status_t rc;

  std::unique_ptr<FdioVolume> volume;
  if ((rc = FdioVolume::Init(std::move(block_dev_fd), std::move(devfs_root_fd), &volume)) !=
      ZX_OK) {
    xprintf("Init failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = volume->Unlock(key, slot)) != ZX_OK) {
    xprintf("Unlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  *out = std::move(volume);
  return ZX_OK;
}

zx_status_t FdioVolume::UnlockWithDeviceKey(fbl::unique_fd block_dev_fd,
                                            fbl::unique_fd devfs_root_fd, key_slot_t slot,
                                            std::unique_ptr<FdioVolume>* out) {
  KeySourcePolicy source;
  zx_status_t rc;
  rc = SelectKeySourcePolicy(&source);
  if (rc != ZX_OK) {
    return rc;
  }

  auto ordered_key_sources = ComputeEffectiveUnsealPolicy(source);
  return TryWithKeysFrom(ordered_key_sources, Activity::Unseal,
                         [&](std::unique_ptr<uint8_t[]> key_buffer, size_t key_size) {
                           crypto::Secret secret;
                           zx_status_t rc;
                           uint8_t* inner;
                           rc = secret.Allocate(key_size, &inner);
                           if (rc != ZX_OK) {
                             xprintf("FdioVolume::UnlockWithDeviceKey: couldn't allocate secret\n");
                             return rc;
                           }
                           memcpy(inner, key_buffer.get(), key_size);
                           rc = FdioVolume::Unlock(std::move(block_dev_fd),
                                                   std::move(devfs_root_fd), secret, slot, out);
                           return rc;
                         });
}

zx_status_t FdioVolume::Unlock(const crypto::Secret& key, key_slot_t slot) {
  return Volume::Unlock(key, slot);
}

// Configuration methods
zx_status_t FdioVolume::Enroll(const crypto::Secret& key, key_slot_t slot) {
  zx_status_t rc;

  if ((rc = SealBlock(key, slot)) != ZX_OK) {
    xprintf("SealBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = CommitBlock()) != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Revoke(key_slot_t slot) {
  zx_status_t rc;

  zx_off_t off;
  crypto::Bytes invalid;
  if ((rc = GetSlotOffset(slot, &off)) != ZX_OK) {
    xprintf("GetSlotOffset failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = invalid.Randomize(slot_len_)) != ZX_OK) {
    xprintf("Randomize failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = block_.Copy(invalid, off)) != ZX_OK) {
    xprintf("Copy failed: %s\n", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = CommitBlock()) != ZX_OK) {
    xprintf("CommitBlock failed: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Init() { return Volume::Init(); }

zx_status_t FdioVolume::OpenManager(const zx::duration& timeout, zx_handle_t* out) {
  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    xprintf("could not convert fd to io\n");
    return ZX_ERR_BAD_STATE;
  }
  return OpenManagerWithCaller(caller, timeout, out);
}

zx_status_t FdioVolume::Open(const zx::duration& timeout, fbl::unique_fd* out) {
  zx_status_t rc;
  fbl::String path_base;

  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    xprintf("could not convert fd to io\n");
    return ZX_ERR_BAD_STATE;
  }

  if ((rc = RelativeTopologicalPath(caller, &path_base)) != ZX_OK) {
    xprintf("could not get topological path: %s\n", zx_status_get_string(rc));
    return rc;
  }
  fbl::String path_block_exposed = fbl::String::Concat({path_base, "/zxcrypt/unsealed/block"});

  // Early return if path_block_exposed is already present in the device tree
  fbl::unique_fd fd(openat(devfs_root_fd_.get(), path_block_exposed.c_str(), O_RDWR));
  if (fd) {
    out->reset(fd.release());
    return ZX_OK;
  }

  // Wait for the unsealed and block devices to bind
  if ((rc = wait_for_device_at(devfs_root_fd_.get(), path_block_exposed.c_str(), timeout.get())) !=
      ZX_OK) {
    xprintf("timed out waiting for %s to exist: %s\n", path_block_exposed.c_str(),
            zx_status_get_string(rc));
    return rc;
  }
  fd.reset(openat(devfs_root_fd_.get(), path_block_exposed.c_str(), O_RDWR));
  if (!fd) {
    xprintf("failed to open zxcrypt volume\n");
    return ZX_ERR_NOT_FOUND;
  }

  out->reset(fd.release());
  return ZX_OK;
}

zx_status_t FdioVolume::GetBlockInfo(BlockInfo* out) {
  zx_status_t rc;
  zx_status_t call_status;
  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_hardware_block_BlockInfo block_info;
  if ((rc = fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &call_status,
                                                &block_info)) != ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  out->block_count = block_info.block_count;
  out->block_size = block_info.block_size;
  return ZX_OK;
}

zx_status_t FdioVolume::GetFvmSliceSize(uint64_t* out) {
  zx_status_t rc;
  zx_status_t call_status;
  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }

  // When this function is called, we're not yet sure if the underlying device
  // actually implements the block protocol, and we use the return value here
  // to tell us if we should utilize FVM-specific codepaths or not.
  // If the underlying channel doesn't respond to volume methods, when we call
  // a method from fuchsia.hardware.block.volume the FIDL channel will be
  // closed and we'll be unable to do other calls to it.  So before making
  // this call, we clone the channel.
  zx::channel channel(fdio_service_clone(caller.borrow_channel()));

  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  if ((rc = fuchsia_hardware_block_volume_VolumeQuery(channel.get(), &call_status, &volume_info)) !=
      ZX_OK) {
    if (rc == ZX_ERR_PEER_CLOSED) {
      // The channel being closed here means that the thing at the other
      // end of this channel does not speak the FVM protocol, and has
      // closed the channel on us.  Return the appropriate error to signal
      // that we shouldn't bother with any of the FVM codepaths.
      return ZX_ERR_NOT_SUPPORTED;
    }
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  *out = volume_info.slice_size;
  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmVsliceQuery(uint64_t vslice_start,
                                              SliceRegion ranges[Volume::MAX_SLICE_REGIONS],
                                              uint64_t* slice_count) {
  static_assert(fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS == Volume::MAX_SLICE_REGIONS,
                "block volume slice response count must match");
  zx_status_t rc;
  zx_status_t call_status;
  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_hardware_block_volume_VsliceRange tmp_ranges[Volume::MAX_SLICE_REGIONS];
  uint64_t range_count;

  if ((rc = fuchsia_hardware_block_volume_VolumeQuerySlices(
           caller.borrow_channel(), &vslice_start, 1, &call_status, tmp_ranges, &range_count)) !=
      ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  if (range_count > Volume::MAX_SLICE_REGIONS) {
    // Should be impossible.  Trust nothing.
    return ZX_ERR_BAD_STATE;
  }

  *slice_count = range_count;
  for (size_t i = 0; i < range_count; i++) {
    ranges[i].allocated = tmp_ranges[i].allocated;
    ranges[i].count = tmp_ranges[i].count;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) {
  zx_status_t rc;
  zx_status_t call_status;
  fzl::UnownedFdioCaller caller(block_dev_fd_.get());
  if (!caller) {
    return ZX_ERR_BAD_STATE;
  }
  if ((rc = fuchsia_hardware_block_volume_VolumeExtend(caller.borrow_channel(), start_slice,
                                                       slice_count, &call_status)) != ZX_OK) {
    return rc;
  }
  if (call_status != ZX_OK) {
    return call_status;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Read() {
  if (lseek(block_dev_fd_.get(), offset_, SEEK_SET) < 0) {
    xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", block_dev_fd_.get(), offset_,
            strerror(errno));
    return ZX_ERR_IO;
  }
  ssize_t res;
  if ((res = read(block_dev_fd_.get(), block_.get(), block_.len())) < 0) {
    xprintf("read(%d, %p, %zu) failed: %s\n", block_dev_fd_.get(), block_.get(), block_.len(),
            strerror(errno));
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(res) != block_.len()) {
    xprintf("short read: have %zd, need %zu\n", res, block_.len());
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::Write() {
  if (lseek(block_dev_fd_.get(), offset_, SEEK_SET) < 0) {
    xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", block_dev_fd_.get(), offset_,
            strerror(errno));
    return ZX_ERR_IO;
  }
  ssize_t res;
  if ((res = write(block_dev_fd_.get(), block_.get(), block_.len())) < 0) {
    xprintf("write(%d, %p, %zu) failed: %s\n", block_dev_fd_.get(), block_.get(), block_.len(),
            strerror(errno));
    return ZX_ERR_IO;
  }
  if (static_cast<size_t>(res) != block_.len()) {
    xprintf("short write: have %zd, need %zu\n", res, block_.len());
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t FdioVolume::OpenManagerWithCaller(fzl::UnownedFdioCaller& caller,
                                              const zx::duration& timeout, zx_handle_t* out) {
  zx_status_t rc;
  fbl::String path_base;

  if ((rc = RelativeTopologicalPath(caller, &path_base)) != ZX_OK) {
    xprintf("could not get topological path: %s\n", zx_status_get_string(rc));
    return rc;
  }
  fbl::String path_manager = fbl::String::Concat({path_base, "/zxcrypt"});

  fbl::unique_fd fd(openat(devfs_root_fd_.get(), path_manager.c_str(), O_RDWR));
  if (!fd) {
    // No manager device in the /dev tree yet.  Try binding the zxcrypt
    // driver and waiting for it to appear.
    auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
        zx::unowned_channel(caller.borrow_channel()),
        ::fidl::StringView(kDriverLib, strlen(kDriverLib)));
    rc = resp.status();
    if (rc == ZX_OK) {
      if (resp->result.is_err()) {
        rc = resp->result.err();
      }
    }
    if (rc != ZX_OK) {
      xprintf("could not bind zxcrypt driver: %s\n", zx_status_get_string(rc));
      return rc;
    }

    // Await the appearance of the zxcrypt device.
    if ((rc = wait_for_device_at(devfs_root_fd_.get(), path_manager.c_str(), timeout.get())) !=
        ZX_OK) {
      xprintf("zxcrypt driver failed to bind: %s\n", zx_status_get_string(rc));
      return rc;
    }

    fd.reset(openat(devfs_root_fd_.get(), path_manager.c_str(), O_RDWR));
    if (!fd) {
      xprintf("failed to open zxcrypt manager\n");
      return ZX_ERR_NOT_FOUND;
    }
  }

  if ((rc = fdio_get_service_handle(fd.release(), out)) != ZX_OK) {
    xprintf("failed to get service handle for zxcrypt manager: %s\n", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t FdioVolume::RelativeTopologicalPath(fzl::UnownedFdioCaller& caller, fbl::String* out) {
  zx_status_t rc;

  // Get the full device path
  fbl::StringBuffer<PATH_MAX> path;
  path.Resize(path.capacity());
  size_t path_len;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(caller.borrow_channel()));
  rc = resp.status();
  if (rc == ZX_OK) {
    if (resp->result.is_err()) {
      rc = resp->result.err();
    } else {
      auto r = resp->result.response();
      path_len = r.path.size();
      memcpy(path.data(), r.path.data(), r.path.size());
    }
  }

  if (rc != ZX_OK) {
    xprintf("could not find parent device: %s\n", zx_status_get_string(rc));
    return rc;
  }

  // Verify that the path returned starts with "/dev/"
  const char* kSlashDevSlash = "/dev/";
  if (path_len < strlen(kSlashDevSlash)) {
    xprintf("path_len way too short: %lu\n", path_len);
    return ZX_ERR_INTERNAL;
  }
  if (strncmp(path.c_str(), kSlashDevSlash, strlen(kSlashDevSlash)) != 0) {
    xprintf("Expected device path to start with '/dev/' but got %s\n", path.c_str());
    return ZX_ERR_INTERNAL;
  }

  // Strip the leading "/dev/" and return the rest
  size_t path_len_sans_dev = path_len - strlen(kSlashDevSlash);
  memmove(path.begin(), path.begin() + strlen(kSlashDevSlash), path_len_sans_dev);

  path.Resize(path_len_sans_dev);
  *out = path.ToString();
  return ZX_OK;
}

}  // namespace zxcrypt
