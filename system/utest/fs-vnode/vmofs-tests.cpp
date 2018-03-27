// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vmofs/vmofs.h>

#include <unittest/unittest.h>

namespace {

bool test_vmofs_file() {
    BEGIN_TEST;

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(64, 0, &vmo);
    EXPECT_EQ(ZX_OK, status);

    status = vmo.write("abcdefghijklmnop", 0, 16);
    EXPECT_EQ(ZX_OK, status);

    auto file = fbl::AdoptRef(new vmofs::VnodeFile(vmo.get(), 0, 3));

    // open
    fbl::RefPtr<fs::Vnode> redirect;
    EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
    EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
    EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
    EXPECT_NULL(redirect);

    // read
    char buffer[1024];
    size_t actual = 0;
    EXPECT_EQ(ZX_OK, file->Read(buffer, 1024, 1, &actual));
    EXPECT_EQ(2, actual);
    EXPECT_EQ('b', buffer[0]);
    EXPECT_EQ('c', buffer[1]);

    // get attr
    vnattr_t attr;
    EXPECT_EQ(ZX_OK, file->Getattr(&attr));
    EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
    EXPECT_EQ(3, attr.size);
    EXPECT_EQ(1, attr.nlink);

    // get handles
    zx_handle_t handle = ZX_HANDLE_INVALID;
    uint32_t type = 0;
    zxrio_object_info_t info;
    EXPECT_EQ(ZX_OK, file->GetHandles(0, &handle, &type, &info));
    EXPECT_NE(ZX_HANDLE_INVALID, handle);
    EXPECT_EQ(ZX_OK, zx_handle_close(handle));
    EXPECT_EQ(FDIO_PROTOCOL_VMOFILE, type);
    EXPECT_EQ(0, info.vmofile.offset);
    EXPECT_EQ(3, info.vmofile.length);

    END_TEST;
}

bool test_vmofs_dir() {
    BEGIN_TEST;

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(64, 0, &vmo);
    EXPECT_EQ(ZX_OK, status);

    status = vmo.write("abcdefghijklmnop", 0, 16);
    EXPECT_EQ(ZX_OK, status);

    fbl::Array<fbl::StringPiece> names(new fbl::StringPiece[3], 3);
    names[0] = "alpha";
    names[1] = "beta";
    names[2] = "gamma";

    fbl::Array<fbl::RefPtr<vmofs::Vnode>> files(new fbl::RefPtr<vmofs::Vnode>[3], 3);
    files[0] = fbl::AdoptRef(new vmofs::VnodeFile(vmo.get(), 0, 8));
    files[1] = fbl::AdoptRef(new vmofs::VnodeFile(vmo.get(), 4, 8));
    files[2] = fbl::AdoptRef(new vmofs::VnodeFile(vmo.get(), 8, 8));

    auto dir = fbl::AdoptRef(new vmofs::VnodeDir(fbl::move(names), fbl::move(files)));

    // open
    fbl::RefPtr<fs::Vnode> redirect;
    EXPECT_EQ(ZX_OK, dir->ValidateFlags(ZX_FS_RIGHT_READABLE));
    EXPECT_EQ(ZX_OK, dir->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
    EXPECT_EQ(ZX_ERR_NOT_FILE, dir->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
    EXPECT_EQ(ZX_OK, dir->Open(ZX_FS_RIGHT_READABLE, &redirect));
    EXPECT_NULL(redirect);

    // get attr
    vnattr_t attr;
    EXPECT_EQ(ZX_OK, dir->Getattr(&attr));
    EXPECT_EQ(V_TYPE_DIR | V_IRUSR, attr.mode);
    EXPECT_EQ(0, attr.size);
    EXPECT_EQ(1, attr.nlink);

    // lookup
    fbl::RefPtr<fs::Vnode> found;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&found, "aaa"));
    EXPECT_FALSE(found);

    EXPECT_EQ(ZX_OK, dir->Lookup(&found, "alpha"));
    char buffer[1024];
    size_t actual = 0;
    EXPECT_EQ(ZX_OK, found->Read(buffer, 1024, 0, &actual));
    EXPECT_EQ(8, actual);
    EXPECT_EQ(0, memcmp(buffer, "abcdefgh", 8));

    found = nullptr;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&found, "bbb"));
    EXPECT_FALSE(found);

    EXPECT_EQ(ZX_OK, dir->Lookup(&found, "beta"));
    actual = 0;
    EXPECT_EQ(ZX_OK, found->Read(buffer, 1024, 0, &actual));
    EXPECT_EQ(8, actual);
    EXPECT_EQ(0, memcmp(buffer, "efghijkl", 8));

    found = nullptr;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&found, "ccc"));
    EXPECT_FALSE(found);

    EXPECT_EQ(ZX_OK, dir->Lookup(&found, "gamma"));
    actual = 0;
    EXPECT_EQ(ZX_OK, found->Read(buffer, 1024, 0, &actual));
    EXPECT_EQ(8, actual);
    EXPECT_EQ(0, memcmp(buffer, "ijklmnop", 8));

    found = nullptr;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&found, "zzz"));
    EXPECT_FALSE(found);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(vmofs_tests)
RUN_TEST(test_vmofs_file)
RUN_TEST(test_vmofs_dir)
END_TEST_CASE(vmofs_tests)
