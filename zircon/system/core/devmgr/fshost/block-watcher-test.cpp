// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>
#include <zxtest/zxtest.h>

#include "block-device-interface.h"

namespace {

class MockBlockDevice : public devmgr::BlockDeviceInterface {
public:
    disk_format_t GetFormat() override = 0;
    void SetFormat(disk_format_t format) override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    bool Netbooting() override { return false; }
    zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) override {
        fuchsia_hardware_block_BlockInfo info = {};
        info.flags = 0;
        info.block_size = 512;
        info.block_count = 1024;
        *out_info = info;
        return ZX_OK;
    }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t AttachDriver(const fbl::StringPiece& driver) override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t UnsealZxcrypt() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t FormatZxcrypt() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    bool ShouldCheckFilesystems() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t CheckFilesystem() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t FormatFilesystem() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
    zx_status_t MountFilesystem() override {
        ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
    }
};

// Tests adding a device which has no GUID and an unknown format.
TEST(AddDeviceTestCase, AddUnknownDevice) {
    class UnknownDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_UNKNOWN;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            return ZX_ERR_NOT_SUPPORTED;
        }
    };
    UnknownDevice device;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding a device with an unknown GUID and unknown format.
TEST(AddDeviceTestCase, AddUnknownPartition) {
    class UnknownDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_UNKNOWN;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
    };
    UnknownDevice device;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding a device with a GPT format.
TEST(AddDeviceTestCase, AddGPTDevice) {
    class GptDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_GPT;
        }
        zx_status_t AttachDriver(const fbl::StringPiece& driver) final {
            EXPECT_STR_EQ(devmgr::kGPTDriverPath, driver.data());
            attached = true;
            return ZX_OK;
        }
        bool attached = false;
    };
    GptDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.attached);
}

// Tests adding a device with an FVM format.
TEST(AddDeviceTestCase, AddFVMDevice) {
    class FvmDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_FVM;
        }
        zx_status_t AttachDriver(const fbl::StringPiece& driver) final {
            EXPECT_STR_EQ(devmgr::kFVMDriverPath, driver.data());
            attached = true;
            return ZX_OK;
        }
        bool attached = false;
    };
    FvmDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.attached);
}

// Tests adding a device with an MBR format.
TEST(AddDeviceTestCase, AddMBRDevice) {
    class MbrDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_MBR;
        }
        zx_status_t AttachDriver(const fbl::StringPiece& driver) final {
            EXPECT_STR_EQ(devmgr::kMBRDriverPath, driver.data());
            attached = true;
            return ZX_OK;
        }
        bool attached = false;
    };
    MbrDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.attached);
}

// Tests adding blobfs which does not not have a type GUID.
TEST(AddDeviceTestCase, AddNoGUIDBlobDevice) {
    class BlobDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_BLOBFS;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            return ZX_ERR_NOT_SUPPORTED;
        }
    };
    BlobDevice device;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding blobfs with a valid type GUID, but invalid metadata.
TEST(AddDeviceTestCase, AddInvalidBlobDevice) {
    class BlobDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_BLOBFS;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_BLOB_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
        zx_status_t CheckFilesystem() final {
            checked = true;
            return ZX_ERR_BAD_STATE;
        }
        zx_status_t FormatFilesystem() final {
            formatted = true;
            return ZX_OK;
        }
        zx_status_t MountFilesystem() final {
            mounted = true;
            return ZX_OK;
        }

        bool checked = false;
        bool formatted = false;
        bool mounted = false;
    };
    BlobDevice device;
    EXPECT_EQ(ZX_ERR_BAD_STATE, device.Add());
    EXPECT_TRUE(device.checked);
    EXPECT_FALSE(device.formatted);
    EXPECT_FALSE(device.mounted);
}

// Tests adding blobfs with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidBlobDevice) {
    class BlobDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_BLOBFS;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_BLOB_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
        zx_status_t CheckFilesystem() final {
            checked = true;
            return ZX_OK;
        }
        zx_status_t FormatFilesystem() final {
            formatted = true;
            return ZX_OK;
        }
        zx_status_t MountFilesystem() final {
            mounted = true;
            return ZX_OK;
        }

        bool checked = false;
        bool formatted = false;
        bool mounted = false;
    };
    BlobDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.checked);
    EXPECT_FALSE(device.formatted);
    EXPECT_TRUE(device.mounted);
}

// Tests adding minfs with a valid type GUID and invalid metadata. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddInvalidMinfsDevice) {
    class MinfsDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_MINFS;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
        zx_status_t CheckFilesystem() final {
            checked = true;
            return ZX_ERR_BAD_STATE;
        }
        zx_status_t FormatFilesystem() final {
            formatted = true;
            return ZX_OK;
        }
        zx_status_t MountFilesystem() final {
            mounted = true;
            return ZX_OK;
        }

        bool checked = false;
        bool formatted = false;
        bool mounted = false;
    };
    MinfsDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.checked);
    EXPECT_TRUE(device.formatted);
    EXPECT_TRUE(device.mounted);
}

// Tests adding minfs with a valid type GUID and invalid format. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddUnknownFormatMinfsDevice) {
    class MinfsDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return format;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
        zx_status_t FormatFilesystem() final {
            formatted = true;
            return ZX_OK;
        }
        zx_status_t CheckFilesystem() final {
            return ZX_OK;
        }
        zx_status_t MountFilesystem() final {
            EXPECT_TRUE(formatted);
            mounted = true;
            return ZX_OK;
        }
        zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
            *is_unsealed_zxcrypt = true;
            return ZX_OK;
        }
        void SetFormat(disk_format_t f) final {
            format = f;
        }

        disk_format_t format = DISK_FORMAT_UNKNOWN;
        bool formatted = false;
        bool mounted = false;
    };
    MinfsDevice device;
    EXPECT_FALSE(device.formatted);
    EXPECT_FALSE(device.mounted);
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.formatted);
    EXPECT_TRUE(device.mounted);
}

// Tests adding zxcrypt with a valid type GUID and invalid format. Observe that
// the partition reformats itself.
TEST(AddDeviceTestCase, AddUnknownFormatZxcryptDevice) {
    class ZxcryptDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return format;
        }
        zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
            const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
            memcpy(out_guid->value, expected, sizeof(expected));
            return ZX_OK;
        }
        zx_status_t FormatZxcrypt() final {
            formatted_zxcrypt = true;
            return ZX_OK;
        }
        zx_status_t FormatFilesystem() final {
            formatted_filesystem = true;
            return ZX_OK;
        }
        zx_status_t CheckFilesystem() final {
            return ZX_OK;
        }
        zx_status_t UnsealZxcrypt() final {
            return ZX_OK;
        }
        zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
            *is_unsealed_zxcrypt = false;
            return ZX_OK;
        }
        void SetFormat(disk_format_t f) final {
            format = f;
        }
        zx_status_t AttachDriver(const fbl::StringPiece& driver) final {
            EXPECT_STR_EQ(devmgr::kZxcryptDriverPath, driver.data());
            return ZX_OK;
        }

        disk_format_t format = DISK_FORMAT_UNKNOWN;
        bool formatted_zxcrypt = false;
        bool formatted_filesystem = false;
    };
    ZxcryptDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_TRUE(device.formatted_zxcrypt);
    EXPECT_FALSE(device.formatted_filesystem);
}

// Tests adding a boot partition device with unknown format can be added with
// the correct driver.
TEST(AddDeviceTestCase, AddUnknownFormatBootPartitionDevice) {
    class BootPartDevice : public MockBlockDevice {
    public:
        disk_format_t GetFormat() final {
            return DISK_FORMAT_UNKNOWN;
        }
        zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) override {
            fuchsia_hardware_block_BlockInfo info = {};
            info.flags = BLOCK_FLAG_BOOTPART;
            info.block_size = 512;
            info.block_count = 1024;
            *out_info = info;
            return ZX_OK;
        }
        zx_status_t AttachDriver(const fbl::StringPiece& driver) final {
            EXPECT_STR_EQ(devmgr::kBootpartDriverPath, driver.data());
            return ZX_OK;
        }
        zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
            *is_unsealed_zxcrypt = false;
            checked_unsealed_zxcrypt = true;
            return ZX_OK;
        }
        bool checked_unsealed_zxcrypt = false;
    };
    BootPartDevice device;
    EXPECT_EQ(ZX_OK, device.Add());
    EXPECT_FALSE(device.checked_unsealed_zxcrypt);
}

} // namespace
