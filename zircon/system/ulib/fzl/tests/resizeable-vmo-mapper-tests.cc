// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/resizeable-vmo-mapper.h>

#include <iterator>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

// Note: these tests focus on the added functionality of the resizable VMO
// mapper.  The core functionality is assumed to have already been tested by the
// vmo/vmar tests.
namespace {

constexpr char vmo_name[ZX_MAX_NAME_LEN] = "my-vmo";
constexpr size_t kNonRootVmarSize = (512 << 20);
constexpr zx_vm_option_t kNonRootVmarOpts =
    ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;

void ValidateCreateHelper(const fzl::ResizeableVmoMapper& mapper, uint64_t size) {
  ASSERT_TRUE(mapper.vmo().is_valid());
  ASSERT_EQ(mapper.size(), size);
  ASSERT_NOT_NULL(mapper.start());

  auto data = static_cast<const uint8_t*>(mapper.start());
  for (size_t i = 0; i < size; ++i) {
    ASSERT_EQ(data[i], 0);
  }

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper.vmo().get_property(ZX_PROP_NAME, name, std::size(name));
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < std::size(name); ++i) {
    ASSERT_EQ(name[i], vmo_name[i]);
  }
}

template <bool NON_ROOT_VMAR>
void UncheckedCreateHelper(std::unique_ptr<fzl::ResizeableVmoMapper>* out_mapper, uint64_t size,
                           const char* name,
                           zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                           uint32_t cache_policy = 0) {
  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NOT_NULL(manager);
  }

  ASSERT_NOT_NULL(out_mapper);
  *out_mapper =
      fzl::ResizeableVmoMapper::Create(size, name, map_options, std::move(manager), cache_policy);
}

template <bool NON_ROOT_VMAR>
void CreateHelper(std::unique_ptr<fzl::ResizeableVmoMapper>* out_mapper, uint64_t size,
                  const char* name, zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                  uint32_t cache_policy = 0) {
  ASSERT_NO_FATAL_FAILURES(
      UncheckedCreateHelper<NON_ROOT_VMAR>(out_mapper, size, name, map_options, cache_policy));
  ASSERT_NOT_NULL(*out_mapper);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(**out_mapper, size));
}

template <bool NON_ROOT_VMAR>
void CreateAndMapHelper(fzl::ResizeableVmoMapper* inout_mapper, uint64_t size, const char* name,
                        zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                        uint32_t cache_policy = 0) {
  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NOT_NULL(manager);
  }

  ASSERT_NOT_NULL(inout_mapper);
  zx_status_t status;
  status = inout_mapper->CreateAndMap(size, name, map_options, std::move(manager), cache_policy);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(*inout_mapper, size));
}

template <bool NON_ROOT_VMAR>
void MapHelper(fzl::ResizeableVmoMapper* inout_mapper, zx::vmo vmo, uint64_t size,
               zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE) {
  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NOT_NULL(manager);
  }

  ASSERT_NOT_NULL(inout_mapper);
  zx_status_t status;
  status = inout_mapper->Map(std::move(vmo), size, map_options, std::move(manager));
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(*inout_mapper, size));
}

template <bool NON_ROOT_VMAR>
void CreateTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));
}

template <bool NON_ROOT_VMAR>
void CreateAndMapTest() {
  fzl::ResizeableVmoMapper mapper;
  ASSERT_NO_FATAL_FAILURES(CreateAndMapHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));
}

template <bool NON_ROOT_VMAR>
void MapTest() {
  zx::vmo vmo;
  zx_status_t status;

  status = zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo);
  ASSERT_EQ(status, ZX_OK);

  status = vmo.set_property(ZX_PROP_NAME, vmo_name, strlen(vmo_name));
  ASSERT_EQ(status, ZX_OK);

  fzl::ResizeableVmoMapper mapper;
  ASSERT_NO_FATAL_FAILURES(MapHelper<NON_ROOT_VMAR>(&mapper, std::move(vmo), ZX_PAGE_SIZE));
}

template <bool NON_ROOT_VMAR>
void MoveTest() {
  fzl::ResizeableVmoMapper mapper1;
  ASSERT_NO_FATAL_FAILURES(CreateAndMapHelper<NON_ROOT_VMAR>(&mapper1, ZX_PAGE_SIZE, vmo_name));

  // Move by construction
  zx_handle_t orig_handle = mapper1.vmo().get();
  void* orig_start = mapper1.start();
  size_t orig_size = mapper1.size();
  const fzl::VmarManager* orig_manager = mapper1.manager().get();

  ASSERT_NE(orig_handle, ZX_HANDLE_INVALID);
  ASSERT_NOT_NULL(orig_start);
  ASSERT_EQ(orig_size, ZX_PAGE_SIZE);
  if (NON_ROOT_VMAR) {
    ASSERT_NOT_NULL(orig_manager);
  } else {
    ASSERT_NULL(orig_manager);
  }

  fzl::ResizeableVmoMapper mapper2(std::move(mapper1));
  ASSERT_EQ(mapper1.vmo().get(), ZX_HANDLE_INVALID);
  ASSERT_NULL(mapper1.start());
  ASSERT_EQ(mapper1.size(), 0);
  ASSERT_NULL(mapper1.manager());

  ASSERT_EQ(mapper2.vmo().get(), orig_handle);
  ASSERT_EQ(mapper2.start(), orig_start);
  ASSERT_EQ(mapper2.size(), orig_size);
  ASSERT_EQ(mapper2.manager().get(), orig_manager);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(mapper2, orig_size));

  // Move by assignment
  mapper1 = std::move(mapper2);

  ASSERT_EQ(mapper2.vmo().get(), ZX_HANDLE_INVALID);
  ASSERT_NULL(mapper2.start());
  ASSERT_EQ(mapper2.size(), 0);
  ASSERT_NULL(mapper2.manager());

  ASSERT_EQ(mapper1.vmo().get(), orig_handle);
  ASSERT_EQ(mapper1.start(), orig_start);
  ASSERT_EQ(mapper1.size(), orig_size);
  ASSERT_EQ(mapper1.manager().get(), orig_manager);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(mapper1, orig_size));
}

template <bool NON_ROOT_VMAR>
void ReadTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  uint8_t bytes[ZX_PAGE_SIZE];
  memset(bytes, 0xff, ZX_PAGE_SIZE);

  zx_status_t status = mapper->vmo().read(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(bytes[i], 0);
  }
}

// Test that touching memory, then zx_vmo_reading, works as expected.
template <bool NON_ROOT_VMAR>
void WriteMappingTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  auto data = static_cast<uint8_t*>(mapper->start());
  memset(data, 0xff, ZX_PAGE_SIZE);

  uint8_t bytes[ZX_PAGE_SIZE] = {};
  zx_status_t status = mapper->vmo().read(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(bytes[i], 0xff);
  }
}

// Test that zx_vmo_writing, then reading memory, works as expected.
template <bool NON_ROOT_VMAR>
void ReadMappingTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  uint8_t bytes[ZX_PAGE_SIZE];
  memset(bytes, 0xff, ZX_PAGE_SIZE);
  zx_status_t status = mapper->vmo().write(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);

  auto data = static_cast<uint8_t*>(mapper->start());
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(data[i], 0xff);
  }
}

template <bool NON_ROOT_VMAR>
void EmptyNameTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, ""));
  ASSERT_NOT_NULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
    ASSERT_EQ(name[i], 0);
  }
}

template <bool NON_ROOT_VMAR>
void NullptrNameTest() {
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, nullptr));
  ASSERT_NOT_NULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
    ASSERT_EQ(name[i], 0);
  }
}

template <bool NON_ROOT_VMAR>
void LongNameTest() {
  char long_name[ZX_PAGE_SIZE];
  memset(long_name, 'x', ZX_PAGE_SIZE);
  long_name[ZX_PAGE_SIZE - 1] = 0;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, long_name));
  ASSERT_NOT_NULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN - 1; ++i) {
    ASSERT_EQ(name[i], 'x');
  }
  ASSERT_EQ(name[ZX_MAX_NAME_LEN - 1], 0);
}

template <bool NON_ROOT_VMAR>
void GoodSizesTest() {
  static const size_t sizes[] = {
      ZX_PAGE_SIZE,
      16 * ZX_PAGE_SIZE,
      ZX_PAGE_SIZE * ZX_PAGE_SIZE,
      ZX_PAGE_SIZE + 1,
  };

  for (size_t size : sizes) {
    std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
    ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, size, vmo_name));
  }
}

template <bool NON_ROOT_VMAR>
void BadSizesTest() {
  // Size 0 should fail.
  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, 0, vmo_name));
  ASSERT_NULL(mapper);

  // So should an aburdly big request.
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, SIZE_MAX, vmo_name));
  ASSERT_NULL(mapper);
}

template <bool NON_ROOT_VMAR>
void GoodShrinkTest() {
  size_t size = ZX_PAGE_SIZE * ZX_PAGE_SIZE;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, size, vmo_name));

  while (size > 2 * ZX_PAGE_SIZE) {
    // The current size.
    zx_status_t status = mapper->Shrink(mapper->size());
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(mapper->size(), size);

    // A paged aligned size.
    size >>= 1;
    status = mapper->Shrink(size);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(mapper->size(), size);
  }

  // TODO: Test that shrinking the map causes subsequent memory
  // accesses to fail.
}

template <bool NON_ROOT_VMAR>
void BadShrinkTest() {
  constexpr size_t size = 16 * ZX_PAGE_SIZE;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, size, vmo_name));

  // Shrinking to 0 should fail.
  zx_status_t status = mapper->Shrink(0);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(mapper->size(), size);

  // Growing via shrink should also fail.
  status = mapper->Shrink(2 * mapper->size());
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(mapper->size(), size);

  // Growing to a misaligned size should also fail.
  status = mapper->Shrink(ZX_PAGE_SIZE + 23);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(mapper->size(), size);
}

template <bool NON_ROOT_VMAR>
void AlignedGoodGrowTest() {
  constexpr size_t original_size = ZX_PAGE_SIZE;
  constexpr size_t grow_size = 2 * ZX_PAGE_SIZE;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, original_size, vmo_name));

  // Growing to the current size should always succeed.
  zx_status_t status = mapper->Grow(mapper->size());
  ASSERT_EQ(status, ZX_OK);

  status = mapper->Grow(grow_size);
  if (status == ZX_OK) {
    ASSERT_EQ(mapper->size(), grow_size);
    // Check the last byte.
    auto data = static_cast<const uint8_t*>(mapper->start());
    ASSERT_EQ(data[grow_size - 1], 0);
  } else {
    // We might just get unlucky and get a ZX_PAGE_SIZE adjacent to
    // something and not be able to grow. If so, assert that the
    // size did not change.
    ASSERT_EQ(mapper->size(), original_size);
  }
}

template <bool NON_ROOT_VMAR>
void UnalignedGoodGrowTest() {
  constexpr size_t original_size = ZX_PAGE_SIZE;
  constexpr size_t grow_size = 2 * ZX_PAGE_SIZE + 1;
  constexpr size_t rounded_grow_size = 3 * ZX_PAGE_SIZE;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, original_size, vmo_name));

  // Growing to the current size should always succeed.
  zx_status_t status = mapper->Grow(mapper->size());
  ASSERT_EQ(status, ZX_OK);

  status = mapper->Grow(grow_size);
  if (status == ZX_OK) {
    ASSERT_EQ(mapper->size(), rounded_grow_size);
    // Check the last byte.
    auto data = static_cast<const uint8_t*>(mapper->start());
    ASSERT_EQ(data[grow_size - 1], 0);
  } else {
    // We might just get unlucky and get a ZX_PAGE_SIZE adjacent to
    // something and not be able to grow. If so, assert that the
    // size did not change.
    ASSERT_EQ(mapper->size(), original_size);
  }
}

template <bool NON_ROOT_VMAR>
void BadGrowTest() {
  constexpr size_t original_size = 2 * ZX_PAGE_SIZE;
  constexpr size_t grow_size = ZX_PAGE_SIZE;

  std::unique_ptr<fzl::ResizeableVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, original_size, vmo_name));

  // Growing from 2 pages to 1 should fail.
  zx_status_t status = mapper->Grow(grow_size);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(mapper->size(), original_size);

  // Growing from 2 pages to nothing should also fail.
  status = mapper->Grow(0);
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(mapper->size(), original_size);
}

}  // namespace

#define MAKE_TEST(_name)                                               \
  TEST(ResizeableVmoMapperTests, _name##_RootVmar) { _name<false>(); } \
  TEST(ResizeableVmoMapperTests, _name##_NON_ROOT_VMAR) { _name<true>(); }

MAKE_TEST(CreateTest)
MAKE_TEST(CreateAndMapTest)
MAKE_TEST(MapTest)
MAKE_TEST(MoveTest)
MAKE_TEST(ReadTest)
MAKE_TEST(WriteMappingTest)
MAKE_TEST(ReadMappingTest)
MAKE_TEST(EmptyNameTest)
MAKE_TEST(NullptrNameTest)
MAKE_TEST(LongNameTest)
MAKE_TEST(GoodSizesTest)
MAKE_TEST(BadSizesTest)
MAKE_TEST(GoodShrinkTest)
MAKE_TEST(BadShrinkTest)
MAKE_TEST(AlignedGoodGrowTest)
MAKE_TEST(UnalignedGoodGrowTest)
MAKE_TEST(BadGrowTest)

#undef MAKE_TEST
