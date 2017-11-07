// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vmo-file.h>

#include <limits.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <unittest/unittest.h>
#include <zx/vmo.h>

namespace {

constexpr size_t VMO_SIZE = PAGE_SIZE * 3u;
constexpr size_t PAGE_0 = 0u;
constexpr size_t PAGE_1 = PAGE_SIZE;
constexpr size_t PAGE_2 = PAGE_SIZE * 2u;

zx_koid_t GetKoid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_rights_t GetRights(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.rights : 0u;
}

bool FillVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t byte) {
    BEGIN_HELPER;

    uint8_t data[length];
    memset(data, byte, length);

    size_t actual = 0u;
    zx_status_t status = vmo.write(data, offset, length, &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(length, actual);

    END_HELPER;
}

bool CheckVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t expected_byte) {
    BEGIN_HELPER;

    uint8_t data[length];

    size_t actual = 0u;
    zx_status_t status = vmo.read(data, offset, length, &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(length, actual);

    for (size_t i = 0; i < length; i++) {
        ASSERT_EQ(expected_byte, data[i]);
    }

    END_HELPER;
}

bool CheckData(uint8_t* data, size_t offset, size_t length, uint8_t expected_byte) {
    BEGIN_HELPER;

    for (size_t i = 0; i < length; i++) {
        ASSERT_EQ(expected_byte, data[i + offset]);
    }

    END_HELPER;
}

bool CreateVmoABC(zx::vmo* out_vmo) {
    BEGIN_HELPER;

    zx_status_t status = zx::vmo::create(VMO_SIZE, 0u, out_vmo);
    ASSERT_EQ(ZX_OK, status);

    ASSERT_TRUE(FillVmo(*out_vmo, PAGE_0, PAGE_SIZE, 'A'));
    ASSERT_TRUE(FillVmo(*out_vmo, PAGE_1, PAGE_SIZE, 'B'));
    ASSERT_TRUE(FillVmo(*out_vmo, PAGE_2, PAGE_SIZE, 'C'));

    END_HELPER;
}

bool test_constructor() {
    BEGIN_TEST;

    zx::vmo abc;
    ASSERT_TRUE(CreateVmoABC(&abc));

    // default parameters
    {
        fs::VmoFile file(abc, 0u, PAGE_SIZE);
        EXPECT_EQ(abc.get(), file.vmo_handle());
        EXPECT_EQ(0u, file.offset());
        EXPECT_EQ(PAGE_SIZE, file.length());
        EXPECT_FALSE(file.is_writable());
        EXPECT_EQ(fs::VmoFile::VmoSharing::DUPLICATE, file.vmo_sharing());
    }

    // everything explicit
    {
        fs::VmoFile file(abc, 3u, PAGE_2 + 1u, true, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(abc.get(), file.vmo_handle());
        EXPECT_EQ(3u, file.offset());
        EXPECT_EQ(PAGE_2 + 1u, file.length());
        EXPECT_TRUE(file.is_writable());
        EXPECT_EQ(fs::VmoFile::VmoSharing::CLONE_COW, file.vmo_sharing());
    }

    END_TEST;
}

bool test_open() {
    BEGIN_TEST;

    zx::vmo abc;
    ASSERT_TRUE(CreateVmoABC(&abc));

    // read-only
    {
        fs::VmoFile file(abc, 0u, 0u);
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file.ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file.Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED,
                  file.ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file.ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_ERR_NOT_DIR, file.ValidateFlags(ZX_FS_FLAG_DIRECTORY));
        EXPECT_NULL(redirect);
    }

    // writable
    {
        fs::VmoFile file(abc, 0u, 0u, true);
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file.ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file.Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_OK, file.ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file.Open(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_OK, file.ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file.Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NULL(redirect);
        EXPECT_EQ(ZX_ERR_NOT_DIR, file.ValidateFlags(ZX_FS_FLAG_DIRECTORY));
        EXPECT_NULL(redirect);
    }

    END_TEST;
}

bool test_read() {
    BEGIN_TEST;

    zx::vmo abc;
    ASSERT_TRUE(CreateVmoABC(&abc));

    uint8_t data[VMO_SIZE];
    memset(data, 0, VMO_SIZE);

    // empty read of non-empty file
    {
        fs::VmoFile file(abc, 0u, PAGE_SIZE);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 0u, 0u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // non-empty read of empty file
    {
        fs::VmoFile file(abc, 0u, 0u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 1u, 0u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // empty read at end of file
    {
        fs::VmoFile file(abc, 0u, 10u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 0u, 10u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // non-empty read at end of file
    {
        fs::VmoFile file(abc, 0u, 10u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 1u, 10u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // empty read beyond end of file
    {
        fs::VmoFile file(abc, 0u, 10u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 0u, 11u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // non-empty read beyond end of file
    {
        fs::VmoFile file(abc, 0u, 10u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 1u, 11u, &actual));
        EXPECT_EQ(0u, actual);
    }

    // short read of non-empty file
    {
        fs::VmoFile file(abc, PAGE_1 - 3u, 10u);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, 11u, 1u, &actual));
        EXPECT_EQ(9u, actual);
        EXPECT_TRUE(CheckData(data, 0u, 2u, 'A'));
        EXPECT_TRUE(CheckData(data, 2u, 7u, 'B'));
    }

    // full read
    {
        fs::VmoFile file(abc, 0u, VMO_SIZE);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Read(data, VMO_SIZE, 0u, &actual));
        EXPECT_EQ(VMO_SIZE, actual);
        EXPECT_TRUE(CheckData(data, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckData(data, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckData(data, PAGE_2, PAGE_SIZE, 'C'));
    }

    END_TEST;
}

bool test_write() {
    BEGIN_TEST;

    zx::vmo abc;
    ASSERT_TRUE(CreateVmoABC(&abc));

    uint8_t data[VMO_SIZE];
    memset(data, '!', VMO_SIZE);

    // empty write of non-empty file
    {
        fs::VmoFile file(abc, 0u, PAGE_SIZE, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Write(data, 0u, 0u, &actual));
        EXPECT_EQ(0u, actual);
        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // non-empty write of empty file
    {
        fs::VmoFile file(abc, 0u, 0u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 0u, &actual));
    }

    // empty write at end of file
    {
        fs::VmoFile file(abc, 0u, 10u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Write(data, 0u, 10u, &actual));
        EXPECT_EQ(0u, actual);
        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // non-empty write at end of file
    {
        fs::VmoFile file(abc, 0u, 10u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 10u, &actual));
    }

    // empty write beyond end of file
    {
        fs::VmoFile file(abc, 0u, 10u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Write(data, 0u, 11u, &actual));
        EXPECT_EQ(0u, actual);
        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // non-empty write beyond end of file
    {
        fs::VmoFile file(abc, 0u, 10u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 11u, &actual));
    }

    // short write of non-empty file
    {
        fs::VmoFile file(abc, PAGE_1 - 3u, 10u, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Write(data, 11u, 1u, &actual));
        EXPECT_EQ(9u, actual);
        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE - 2u, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 - 2u, 9u, '!'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 + 7u, PAGE_SIZE - 7u, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // full write
    {
        fs::VmoFile file(abc, 0u, VMO_SIZE, true);
        size_t actual = UINT64_MAX;
        EXPECT_EQ(ZX_OK, file.Write(data, VMO_SIZE, 0u, &actual));
        EXPECT_EQ(VMO_SIZE, actual);
        EXPECT_TRUE(CheckVmo(abc, 0u, VMO_SIZE, '!'));
    }

    END_TEST;
}

bool test_getattr() {
    BEGIN_TEST;

    zx::vmo abc;
    ASSERT_TRUE(CreateVmoABC(&abc));

    // read-only
    {
        fs::VmoFile file(abc, 0u, PAGE_SIZE * 3u + 117u);
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file.Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
        EXPECT_EQ(PAGE_SIZE * 3u + 117u, attr.size);
        EXPECT_EQ(PAGE_SIZE, attr.blksize);
        EXPECT_EQ(4u * PAGE_SIZE / VNATTR_BLKSIZE, attr.blkcount);
        EXPECT_EQ(1u, attr.nlink);
    }

    // writable
    {
        fs::VmoFile file(abc, 0u, PAGE_SIZE * 3u + 117u, true);
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file.Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR | V_IWUSR, attr.mode);
        EXPECT_EQ(PAGE_SIZE * 3u + 117u, attr.size);
        EXPECT_EQ(PAGE_SIZE, attr.blksize);
        EXPECT_EQ(4u * PAGE_SIZE / VNATTR_BLKSIZE, attr.blkcount);
        EXPECT_EQ(1u, attr.nlink);
    }

    END_TEST;
}

bool test_get_handles() {
    BEGIN_TEST;

    // sharing = VmoSharing::NONE
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::NONE);
        EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, file.GetHandles(ZX_FS_RIGHT_READABLE,
                                                        vmo.reset_and_get_address(),
                                                        &handle_count, &type, info, &info_size));
    }

    // sharing = VmoSharing::DUPLICATE, read only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_READABLE, vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_EXECUTE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_1 - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_1, 18u, 'B'));
    }

    // sharing = VmoSharing::DUPLICATE, read-write
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                                         vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_1 - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_1, 18u, 'B'));

        EXPECT_TRUE(FillVmo(vmo, PAGE_1 - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 - 5u, 23u, '!'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::DUPLICATE, write only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_WRITABLE, vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_1 - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(FillVmo(vmo, PAGE_1 - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 - 5u, 23u, '!'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, read only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, false, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_READABLE, vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_EXECUTE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_SIZE - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE, 18u, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, read-write
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                                         vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_SIZE - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE, 18u, 'C'));

        EXPECT_TRUE(FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, write only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t handle_count;
        uint32_t type;
        zx_off_t info[2];
        uint32_t info_size;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.GetHandles(ZX_FS_RIGHT_WRITABLE, vmo.reset_and_get_address(),
                                         &handle_count, &type, info, &info_size));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(1u, handle_count);
        EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
        EXPECT_EQ(PAGE_SIZE - 5u, info[0]);
        EXPECT_EQ(23u, info[1]);
        EXPECT_EQ(sizeof(info), info_size);

        EXPECT_TRUE(FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    END_TEST;
}

bool test_mmap() {
    BEGIN_TEST;

    // sharing = VmoSharing::NONE
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::NONE);
        EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, file.Mmap(FDIO_MMAP_FLAG_READ, PAGE_SIZE,
                                                  &offset, vmo.reset_and_get_address()));
    }

    // sharing = VmoSharing::DUPLICATE, read-exec
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_EXEC,
                                   PAGE_SIZE, &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_EXECUTE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_1 - 5u, offset);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_1, 18u, 'B'));
    }

    // sharing = VmoSharing::DUPLICATE, read-write
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_WRITE,
                                   PAGE_SIZE, &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_1 - 5u, offset);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_1, 18u, 'B'));

        EXPECT_TRUE(FillVmo(vmo, PAGE_1 - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 - 5u, 23u, '!'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::DUPLICATE, write only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_WRITE, PAGE_SIZE,
                                   &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_1 - 5u, offset);

        EXPECT_TRUE(FillVmo(vmo, PAGE_1 - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 - 5u, 23u, '!'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, read-exec
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, false, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_EXEC,
                                   PAGE_SIZE, &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_EXECUTE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_SIZE - 5u, offset);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE, 18u, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, read-write
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_WRITE,
                                   PAGE_SIZE, &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_SIZE - 5u, offset);

        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B'));
        EXPECT_TRUE(CheckVmo(vmo, PAGE_SIZE, 18u, 'C'));

        EXPECT_TRUE(FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    // sharing = VmoSharing::CLONE_COW, write only
    {
        zx::vmo abc;
        ASSERT_TRUE(CreateVmoABC(&abc));

        zx::vmo vmo;
        size_t offset;
        fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
        EXPECT_EQ(ZX_OK, file.Mmap(FDIO_MMAP_FLAG_WRITE, PAGE_SIZE,
                                   &offset, vmo.reset_and_get_address()));
        EXPECT_NE(abc.get(), vmo.get());
        EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
        EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_MAP |
                      ZX_RIGHT_WRITE,
                  GetRights(vmo.get()));
        EXPECT_EQ(PAGE_SIZE - 5u, offset);

        EXPECT_TRUE(FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!'));

        EXPECT_TRUE(CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B'));
        EXPECT_TRUE(CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C'));
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(vmo_file_tests)
RUN_TEST(test_constructor)
RUN_TEST(test_open)
RUN_TEST(test_read)
RUN_TEST(test_write)
RUN_TEST(test_getattr)
RUN_TEST(test_get_handles)
RUN_TEST(test_mmap)
END_TEST_CASE(vmo_file_tests)
