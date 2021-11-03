// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/io.h>
#include <lib/inspect/cpp/reader.h>
#include <zircon/errors.h>

#include <ostream>

#include <fbl/string.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "fbl/unique_fd.h"
#include "fuchsia/io/cpp/fidl.h"
#include "lib/fidl/llcpp/client_end.h"
#include "src/security/zxcrypt/client.h"
#include "src/security/zxcrypt/volume.h"

namespace {
constexpr zx::duration kTimeout = zx::sec(3);
constexpr uint32_t kBlockSz = 512;
constexpr uint32_t kBlockCnt = 20;

std::string GetInspectInstanceGuid(const zx::vmo& inspect_vmo) {
  auto base_hierarchy = inspect::ReadFromVmo(inspect_vmo).take_value();
  auto* hierarchy = base_hierarchy.GetByPath({"zxcrypt0x0"});
  if (hierarchy == nullptr) {
    return "";
  }
  auto* property = hierarchy->node().get_property<inspect::StringPropertyValue>("instance_guid");
  if (property == nullptr) {
    return "";
  }
  return property->value();
}

zx::vmo GetInspectVMOHandle(const fbl::unique_fd& devfs_root) {
  fbl::unique_fd fd;
  zx_status_t rc;
  if ((rc = devmgr_integration_test::RecursiveWaitForFileReadOnly(
           devfs_root, "diagnostics/class/zxcrypt/000.inspect", &fd)) != ZX_OK) {
    printf("Failed in wait for inspect file: %d\n", rc);
    return zx::vmo();
  }
  zx_handle_t out_vmo = ZX_HANDLE_INVALID;
  if ((rc = fdio_get_vmo_clone(fd.get(), &out_vmo)) != ZX_OK) {
    printf("Failed to clone inspect VMO: %d\n", rc);
    return zx::vmo();
  }
  return zx::vmo(out_vmo);
}

TEST(ZxcryptInspect, ExportsGuid) {
  // Zxcrypt volume manager requires this.
  driver_integration_test::IsolatedDevmgr devmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  args.disable_block_watcher = true;
  ASSERT_EQ(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr), ZX_OK);
  fbl::unique_fd ctl;
  ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                          "sys/platform/00:00:2d/ramctl", &ctl),
            ZX_OK);

  fbl::unique_fd devfs_root_fd = devmgr.devfs_root().duplicate();

  // Create a new ramdisk to stick our zxcrypt instance on.
  ramdisk_client_t* ramdisk = nullptr;
  ASSERT_OK(ramdisk_create_at(devmgr.devfs_root().get(), kBlockSz, kBlockCnt, &ramdisk));
  fbl::unique_fd ramdisk_ignored;
  devmgr_integration_test::RecursiveWaitForFile(devfs_root_fd, ramdisk_get_path(ramdisk),
                                                &ramdisk_ignored);
  fbl::unique_fd ramdisk_fd = fbl::unique_fd(dup(ramdisk_get_block_fd(ramdisk)));

  // Create a new zxcrypt volume manager using the ramdisk.
  auto vol_mgr =
      std::make_unique<zxcrypt::VolumeManager>(std::move(ramdisk_fd), std::move(devfs_root_fd));
  zx::channel zxc_client_chan;
  ASSERT_OK(vol_mgr->OpenClient(kTimeout, zxc_client_chan));

  // Create a new crypto key.
  crypto::Secret key;
  size_t digest_len;
  ASSERT_OK(crypto::digest::GetDigestLen(crypto::digest::kSHA256, &digest_len));
  ASSERT_OK(key.Generate(digest_len));

  // Unsealing should fail right now until we format. It'll look like a bad key error, but really we
  // haven't even got a formatted device yet.
  zxcrypt::EncryptedVolumeClient volume_client(std::move(zxc_client_chan));
  ASSERT_EQ(volume_client.Unseal(key.get(), key.len(), 0), ZX_ERR_ACCESS_DENIED);
  ASSERT_TRUE(GetInspectInstanceGuid(GetInspectVMOHandle(devmgr.devfs_root())).empty());

  // After formatting, we should be able to unseal a device and see its GUID in inspect.
  ASSERT_OK(volume_client.Format(key.get(), key.len(), 0));

  ASSERT_OK(volume_client.Unseal(key.get(), key.len(), 0));
  std::string guid = GetInspectInstanceGuid(GetInspectVMOHandle(devmgr.devfs_root()));
  ASSERT_FALSE(guid.empty());

  // Seal and confirm that the GUID is gone.
  ASSERT_OK(volume_client.Seal());
  ASSERT_TRUE(GetInspectInstanceGuid(GetInspectVMOHandle(devmgr.devfs_root())).empty());

  // Ensure we turn down the zxcrypt manager before we free up the ramdisk.
  vol_mgr.reset();
  ramdisk_destroy(ramdisk);
}

}  // namespace
