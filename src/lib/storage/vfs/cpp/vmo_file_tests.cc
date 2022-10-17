// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file->

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"

namespace {

using VnodeOptions = fs::VnodeConnectionOptions;

const size_t VMO_SIZE = zx_system_get_page_size() * 3u;
const size_t PAGE_0 = 0u;
const size_t PAGE_1 = zx_system_get_page_size();
const size_t PAGE_2 = zx_system_get_page_size() * 2u;

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_rights_t GetRights(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : 0u;
}

void FillVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t byte) {
  uint8_t data[length];
  memset(data, byte, length);

  zx_status_t status = vmo.write(data, offset, length);
  ASSERT_EQ(ZX_OK, status);
}

void CheckVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t expected_byte) {
  uint8_t data[length];

  zx_status_t status = vmo.read(data, offset, length);
  ASSERT_EQ(ZX_OK, status);

  for (size_t i = 0; i < length; i++) {
    ASSERT_EQ(expected_byte, data[i]);
  }
}

void CheckData(uint8_t* data, size_t offset, size_t length, uint8_t expected_byte) {
  for (size_t i = 0; i < length; i++) {
    ASSERT_EQ(expected_byte, data[i + offset]);
  }
}

void CreateVmoABC(zx::vmo* out_vmo) {
  zx_status_t status = zx::vmo::create(VMO_SIZE, 0u, out_vmo);
  ASSERT_EQ(ZX_OK, status);

  FillVmo(*out_vmo, PAGE_0, zx_system_get_page_size(), 'A');
  FillVmo(*out_vmo, PAGE_1, zx_system_get_page_size(), 'B');
  FillVmo(*out_vmo, PAGE_2, zx_system_get_page_size(), 'C');
}

TEST(VmoFile, Constructor) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // default parameters
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), zx_system_get_page_size());
    EXPECT_EQ(zx_system_get_page_size(), file->length());
    EXPECT_FALSE(file->is_writable());
    EXPECT_EQ(fs::VmoFile::VmoSharing::DUPLICATE, file->vmo_sharing());
  }

  // everything explicit
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), PAGE_2 + 1u, true,
                                                 fs::VmoFile::VmoSharing::CLONE_COW);
    EXPECT_EQ(PAGE_2 + 1u, file->length());
    EXPECT_TRUE(file->is_writable());
    EXPECT_EQ(fs::VmoFile::VmoSharing::CLONE_COW, file->vmo_sharing());
  }
}

#define EXPECT_RESULT_OK(expr) EXPECT_TRUE((expr).is_ok())
#define EXPECT_RESULT_ERROR(error_val, expr) \
  EXPECT_TRUE((expr).is_error());            \
  EXPECT_EQ(error_val, (expr).status_value())

TEST(VmoFile, Open) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // read-only
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 0u, 0u);
    fbl::RefPtr<fs::Vnode> redirect;
    auto result = file->ValidateOptions(VnodeOptions::ReadOnly());
    EXPECT_RESULT_OK(result);
    EXPECT_EQ(ZX_OK, file->Open(result.value(), &redirect));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file->ValidateOptions(VnodeOptions::ReadWrite()));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file->ValidateOptions(VnodeOptions::WriteOnly()));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file->ValidateOptions(VnodeOptions::ReadExec()));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR, file->ValidateOptions(VnodeOptions().set_directory()));
    EXPECT_NULL(redirect);
  }

  // writable
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 0u, true);
    fbl::RefPtr<fs::Vnode> redirect;
    {
      zx::result result = file->ValidateOptions(VnodeOptions::ReadOnly());
      EXPECT_RESULT_OK(result);
      EXPECT_EQ(ZX_OK, file->Open(result.value(), &redirect));
      EXPECT_NULL(redirect);
    }
    {
      zx::result result = file->ValidateOptions(VnodeOptions::ReadWrite());
      EXPECT_RESULT_OK(result);
      EXPECT_EQ(ZX_OK, file->Open(result.value(), &redirect));
      EXPECT_NULL(redirect);
    }
    {
      zx::result result = file->ValidateOptions(VnodeOptions::WriteOnly());
      EXPECT_RESULT_OK(result);
      EXPECT_EQ(ZX_OK, file->Open(result.value(), &redirect));
      EXPECT_NULL(redirect);
      EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file->ValidateOptions(VnodeOptions::ReadExec()));
      EXPECT_NULL(redirect);
      EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR, file->ValidateOptions(VnodeOptions().set_directory()));
      EXPECT_NULL(redirect);
    }
  }
}

TEST(VmoFile, Read) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  uint8_t data[VMO_SIZE];
  memset(data, 0, VMO_SIZE);

  {
    SCOPED_TRACE("empty-read-nonempty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 0u, zx_system_get_page_size());
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 0u, 0u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("nonempty-read-empty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 0u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 1u, 0u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("empty-read-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 0u, 10u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("nonempty-read-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 1u, 10u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("empty-read-beyond-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 0u, 11u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("nonempty-read-beyond-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 1u, 11u, &actual));
    EXPECT_EQ(0u, actual);
  }

  {
    SCOPED_TRACE("short-read-nonempty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, 11u, 1u, &actual));
    EXPECT_EQ(9u, actual);
    CheckData(data, 0u, 9u, 'A');
    CheckData(data, 9u, 1, 0);
  }

  {
    SCOPED_TRACE("full-read");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), VMO_SIZE);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Read(data, VMO_SIZE, 0u, &actual));
    EXPECT_EQ(VMO_SIZE, actual);
    CheckData(data, PAGE_0, zx_system_get_page_size(), 'A');
    CheckData(data, PAGE_1, zx_system_get_page_size(), 'B');
    CheckData(data, PAGE_2, zx_system_get_page_size(), 'C');
  }
}

TEST(VmoFile, Write) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  uint8_t data[VMO_SIZE];
  memset(data, '!', VMO_SIZE);

  {
    SCOPED_TRACE("empty-write-nonempty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), zx_system_get_page_size(), true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Write(data, 0u, 0u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, zx_system_get_page_size(), 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }

  {
    SCOPED_TRACE("nonempty-write-empty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 0u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file->Write(data, 1u, 0u, &actual));
  }

  {
    SCOPED_TRACE("empty-write-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Write(data, 0u, 10u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, zx_system_get_page_size(), 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }

  {
    SCOPED_TRACE("nonempty-write-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file->Write(data, 1u, 10u, &actual));
  }

  {
    SCOPED_TRACE("empty-write-beyond-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Write(data, 0u, 11u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, zx_system_get_page_size(), 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }

  {
    SCOPED_TRACE("nonempty-write-beyond-end-of-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file->Write(data, 1u, 11u, &actual));
  }

  {
    SCOPED_TRACE("short-write-nonempty-file");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Write(data, 11u, 1u, &actual));
    EXPECT_EQ(9u, actual);
    CheckVmo(abc, PAGE_0, 1, 'A');
    CheckVmo(abc, PAGE_0 + 1, 9u, '!');
    CheckVmo(abc, PAGE_0 + 10, zx_system_get_page_size() - 10, 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }

  {
    SCOPED_TRACE("full-write");
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), VMO_SIZE, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file->Write(data, VMO_SIZE, 0u, &actual));
    EXPECT_EQ(VMO_SIZE, actual);
    CheckVmo(abc, 0u, VMO_SIZE, '!');
  }
}

TEST(VmoFile, Getattr) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // read-only
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file =
        fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), zx_system_get_page_size() * 3u + 117u);
    fs::VnodeAttributes attr;
    EXPECT_EQ(ZX_OK, file->GetAttributes(&attr));
    EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
    EXPECT_EQ(zx_system_get_page_size() * 3u + 117u, attr.content_size);
    EXPECT_EQ(4u * zx_system_get_page_size(), attr.storage_size);
    EXPECT_EQ(1u, attr.link_count);
  }

  // writable
  {
    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup),
                                                 zx_system_get_page_size() * 3u + 117u, true);
    fs::VnodeAttributes attr;
    EXPECT_EQ(ZX_OK, file->GetAttributes(&attr));
    EXPECT_EQ(V_TYPE_FILE | V_IRUSR | V_IWUSR, attr.mode);
    EXPECT_EQ(zx_system_get_page_size() * 3u + 117u, attr.content_size);
    EXPECT_EQ(4u * zx_system_get_page_size(), attr.storage_size);
    EXPECT_EQ(1u, attr.link_count);
  }
}

TEST(VmoFile, GetNodeInfo) {
  {
    SCOPED_TRACE("VmoSharing::NONE");
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    auto file =
        fbl::MakeRefCounted<fs::VmoFile>(std::move(abc), 23u, false, fs::VmoFile::VmoSharing::NONE);
    EXPECT_EQ(ZX_OK, file->GetNodeInfo(fs::Rights::ReadOnly(), &info));
    EXPECT_TRUE(info.is_file());
  }

  {
    SCOPED_TRACE("VmoSharing::DUPLICATE,read-only");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, false,
                                                 fs::VmoFile::VmoSharing::DUPLICATE);
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, file->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo));

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ,
              GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(VMO_SIZE, size);

    CheckVmo(vmo, PAGE_0, 23u, 'A');
  }

  {
    SCOPED_TRACE("VmoSharing::DUPLICATE,read-write");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, true,
                                                 fs::VmoFile::VmoSharing::DUPLICATE);
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, file->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo));

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));

    // As the VmoFile implementation does not currently track size changes, we ensure that the
    // handle provided in DUPLICATE sharing mode is not writable.
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ,
              GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(VMO_SIZE, size);

    CheckVmo(vmo, PAGE_0, 23u, 'A');
  }

  {
    SCOPED_TRACE("VmoSharing::DUPLICATE,write-only");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, true,
                                                 fs::VmoFile::VmoSharing::DUPLICATE);
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, file->GetVmo({}, &vmo));

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
    // As the VmoFile implementation does not currently track size changes, we ensure that the
    // handle provided in DUPLICATE sharing mode is not writable.
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY, GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(VMO_SIZE, size);
  }

  {
    SCOPED_TRACE("VmoSharing::CLONE_COW,read-only");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, false,
                                                 fs::VmoFile::VmoSharing::CLONE_COW);
    zx::vmo vmo;
    // There is non-trivial lazy initialization happening here - repeat it
    // to make sure it's nice and deterministic.
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(ZX_OK, file->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo));
    }

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ,
              GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(23u, size);

    CheckVmo(vmo, PAGE_0, 23u, 'A');
  }

  {
    SCOPED_TRACE("VmoSharing::CLONE_COW,read-write");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, true,
                                                 fs::VmoFile::VmoSharing::CLONE_COW);
    zx::vmo vmo;
    EXPECT_EQ(
        ZX_OK,
        file->GetVmo(fuchsia_io::wire::VmoFlags::kRead | fuchsia_io::wire::VmoFlags::kWrite, &vmo));

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ |
                  ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY,
              GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(23u, size);

    FillVmo(vmo, 0, 23u, '!');

    CheckVmo(abc, PAGE_0, zx_system_get_page_size(), 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }

  {
    SCOPED_TRACE("VmoSharing::CLONE_COW,write-only");
    zx::vmo abc;
    CreateVmoABC(&abc);

    zx::vmo dup;
    ASSERT_EQ(ZX_OK, abc.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    auto file = fbl::MakeRefCounted<fs::VmoFile>(std::move(dup), 23u, true,
                                                 fs::VmoFile::VmoSharing::CLONE_COW);
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, file->GetVmo(fuchsia_io::wire::VmoFlags::kWrite, &vmo));

    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_WRITE |
                  ZX_RIGHT_SET_PROPERTY,
              GetRights(vmo.get()));
    uint64_t size;
    EXPECT_EQ(ZX_OK, vmo.get_prop_content_size(&size));
    EXPECT_EQ(23u, size);

    FillVmo(vmo, 0, 23u, '!');

    CheckVmo(abc, PAGE_0, zx_system_get_page_size(), 'A');
    CheckVmo(abc, PAGE_1, zx_system_get_page_size(), 'B');
    CheckVmo(abc, PAGE_2, zx_system_get_page_size(), 'C');
  }
}

}  // namespace
