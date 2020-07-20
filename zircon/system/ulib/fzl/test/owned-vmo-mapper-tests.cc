// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>

#include <iterator>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

// Note: these tests focus on the added functionality of the owned VMO
// mapper.  The core functionality is assumed to have already been tested by the
// vmo/vmar tests.
namespace {

constexpr char vmo_name[ZX_MAX_NAME_LEN] = "my-vmo";
constexpr size_t kNonRootVmarSize = (512 << 20);
constexpr zx_vm_option_t kNonRootVmarOpts =
    ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;

void ValidateCreateHelper(const fzl::OwnedVmoMapper& mapper, uint64_t size) {
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
void UncheckedCreateHelper(std::unique_ptr<fzl::OwnedVmoMapper>* out_mapper, uint64_t size,
                           const char* name,
                           zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                           uint32_t cache_policy = 0) {
  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NOT_NULL(manager);
  }

  ASSERT_NOT_NULL(out_mapper);
  auto mapper = std::make_unique<fzl::OwnedVmoMapper>();
  if (mapper->CreateAndMap(size, name, map_options, std::move(manager), cache_policy) == ZX_OK) {
    *out_mapper = std::move(mapper);
  }
}

template <bool NON_ROOT_VMAR>
void CreateHelper(std::unique_ptr<fzl::OwnedVmoMapper>* out_mapper, uint64_t size, const char* name,
                  zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                  uint32_t cache_policy = 0) {
  ASSERT_NO_FATAL_FAILURES(
      UncheckedCreateHelper<NON_ROOT_VMAR>(out_mapper, size, name, map_options, cache_policy));
  ASSERT_NOT_NULL(*out_mapper);
  ASSERT_NO_FATAL_FAILURES(ValidateCreateHelper(**out_mapper, size));
}

template <bool NON_ROOT_VMAR>
void CreateAndMapHelper(fzl::OwnedVmoMapper* inout_mapper, uint64_t size, const char* name,
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
void MapHelper(fzl::OwnedVmoMapper* inout_mapper, zx::vmo vmo, uint64_t size,
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));
}

template <bool NON_ROOT_VMAR>
void CreateAndMapTest() {
  fzl::OwnedVmoMapper mapper;
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

  fzl::OwnedVmoMapper mapper;
  ASSERT_NO_FATAL_FAILURES(MapHelper<NON_ROOT_VMAR>(&mapper, std::move(vmo), ZX_PAGE_SIZE));
}

template <bool NON_ROOT_VMAR>
void MoveTest() {
  fzl::OwnedVmoMapper mapper1;
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

  fzl::OwnedVmoMapper mapper2(std::move(mapper1));
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
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
    std::unique_ptr<fzl::OwnedVmoMapper> mapper;
    ASSERT_NO_FATAL_FAILURES(CreateHelper<NON_ROOT_VMAR>(&mapper, size, vmo_name));
  }
}

template <bool NON_ROOT_VMAR>
void BadSizesTest() {
  // Size 0 should fail.
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, 0, vmo_name));
  ASSERT_NULL(mapper);

  // So should an aburdly big request.
  ASSERT_NO_FATAL_FAILURES(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, SIZE_MAX, vmo_name));
  ASSERT_NULL(mapper);
}

}  // namespace

#define MAKE_TEST(_name)                                          \
  TEST(OwnedVmoMapperTests, _name##_RootVmar) { _name<false>(); } \
  TEST(OwnedVmoMapperTests, _name##_NON_ROOT_VMAR) { _name<true>(); }

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

#undef MAKE_TEST
