// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encrypted-volume.h"

#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <zircon/status.h>

#include <zxcrypt/fdio-volume.h>

namespace devmgr {

EncryptedVolume::EncryptedVolume(fbl::unique_fd fd, fbl::unique_fd devfs_root)
    : fd_(std::move(fd)), devfs_root_(std::move(devfs_root)) {}

zx_status_t EncryptedVolume::Unseal() {
  zx_status_t rc;
  std::unique_ptr<zxcrypt::FdioVolume> zxcrypt_volume;
  rc = zxcrypt::FdioVolume::Init(fd_.duplicate(), devfs_root_.duplicate(), &zxcrypt_volume);
  if (rc != ZX_OK) {
    FX_LOGS(ERROR) << "couldn't open zxcrypt fdio volume: " << zx_status_get_string(rc);
    return rc;
  }

  zx::channel zxcrypt_volume_manager_chan;
  rc = zxcrypt_volume->OpenManager(zx::sec(2), zxcrypt_volume_manager_chan.reset_and_get_address());
  if (rc != ZX_OK) {
    FX_LOGS(ERROR) << "couldn't open zxcrypt manager device: " << zx_status_get_string(rc);
    return rc;
  }

  zxcrypt::FdioVolumeManager zxcrypt_volume_manager(std::move(zxcrypt_volume_manager_chan));
  uint8_t slot = 0;
  rc = zxcrypt_volume_manager.UnsealWithDeviceKey(slot);
  if (rc != ZX_OK) {
    FX_LOGS(ERROR) << "couldn't unseal zxcrypt manager device: " << zx_status_get_string(rc);
    return rc;
  }

  return ZX_OK;
}

zx_status_t EncryptedVolume::Format() {
  zx_status_t rc;
  rc = zxcrypt::FdioVolume::CreateWithDeviceKey(fd_.duplicate(), devfs_root_.duplicate(), nullptr);
  if (rc != ZX_OK) {
    FX_LOGS(ERROR) << "couldn't format zxcrypt volume with device key: "
                   << zx_status_get_string(rc);
    return rc;
  }

  return ZX_OK;
}

const int kUnsealTryCountBeforeWipe = 5;

zx_status_t EncryptedVolumeInterface::EnsureUnsealedAndFormatIfNeeded() {
  // Policy: first, unseal.  If that fails, format, then unseal again.
  zx_status_t rc;
  int try_count = 0;
  do {
    rc = Unseal();
    try_count++;
  } while (rc != ZX_OK && try_count < kUnsealTryCountBeforeWipe);

  if (rc == ZX_OK) {
    // We successfully unsealed the volume.  No need to wipe.  Return success.
    return ZX_OK;
  }

  // Alas, we could not unseal the volume.  Give up.  If the error code suggests
  // we just have the wrong key, try formatting the volume with the keys we
  // have.  Otherwise, just return the error we got from the last Unseal()
  // attempt.
  if (rc == ZX_ERR_ACCESS_DENIED) {
    fprintf(stderr,
            "fshost: Failed repeatedly to unseal zxcrypt device with all available keys.  "
            "Destructively reformatting with new key to attempt to bring up an empty block volume "
            "rather than none at all.  Expect factory-reset-like behavior.\n");
    rc = Format();
    if (rc != ZX_OK) {
      FX_LOGS(ERROR) << "couldn't format encrypted volume: " << zx_status_get_string(rc);
      return rc;
    }

    // At this point, we had better be able to unseal the volume that we just
    // formatted.
    rc = Unseal();
    if (rc != ZX_OK) {
      FX_LOGS(ERROR) << "formatted volume but couldn't unseal it thereafter: "
                     << zx_status_get_string(rc);
      return rc;
    }

    return ZX_OK;
  } else {
    FX_LOGS(ERROR) << "could not produce an unsealed volume for minfs: "
                   << zx_status_get_string(rc);
    return rc;
  }
}

}  // namespace devmgr
