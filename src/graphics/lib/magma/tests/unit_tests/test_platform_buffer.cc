// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "magma_util/macros.h"
#include "platform_buffer.h"

// Some tests don't support ASAN
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAS_FEATURE_ASAN
#endif
#endif

#if defined(__Fuchsia__)

#include <lib/zx/vmar.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>

static uint32_t GetVmarHandle(uint64_t size) {
  zx::vmar test_vmar;
  uint64_t child_addr;
  EXPECT_EQ(ZX_OK,
            zx::vmar::root_self()->allocate2(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,  // flags
                                             0,                                         // offset,
                                             size,                                      // size
                                             &test_vmar,                                // child
                                             &child_addr  // child_addr
                                             ));
  return test_vmar.release();
}

#endif

class TestPlatformBuffer {
 public:
  static void Basic(uint64_t size) {
    std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(size, "test");
    if (size == 0) {
      EXPECT_FALSE(buffer);
      return;
    }

    ASSERT_TRUE(buffer);
    ASSERT_TRUE(buffer->size() % magma::page_size() == 0);
    EXPECT_GE(buffer->size(), size);

    void* virt_addr = nullptr;
    EXPECT_TRUE(buffer->MapCpu(&virt_addr));
    ASSERT_TRUE(virt_addr);

    // write first word
    static const uint32_t first_word = 0xdeadbeef;
    static const uint32_t last_word = 0x12345678;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr)) = first_word;
    // write last word
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) + buffer->size() - 4) =
        last_word;

    EXPECT_TRUE(buffer->UnmapCpu());
    // remap and check
    ASSERT_TRUE(buffer->MapCpu(&virt_addr));
    EXPECT_EQ(first_word, *reinterpret_cast<uint32_t*>(virt_addr));
    EXPECT_EQ(last_word, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) +
                                                      buffer->size() - 4));
    EXPECT_TRUE(buffer->UnmapCpu());

    // check again
    ASSERT_TRUE(buffer->MapCpu(&virt_addr));
    EXPECT_EQ(first_word, *reinterpret_cast<uint32_t*>(virt_addr));
    EXPECT_EQ(last_word, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) +
                                                      buffer->size() - 4));
    EXPECT_TRUE(buffer->UnmapCpu());
  }

#if defined(__Fuchsia__)
  static void MapSpecific() {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size() * 2, "test");
    // Unaligned
    EXPECT_FALSE(buffer->MapAtCpuAddr(0x1000001, 0, magma::page_size()));

    // Below bottom of root vmar
    EXPECT_FALSE(buffer->MapAtCpuAddr(magma::page_size(), 0, magma::page_size()));
    uint64_t addr = 0x10000000;
    uint32_t i;
    // Try multiple times in case something is already mapped there.
    for (i = 0; i < 100; i++) {
      addr += magma::page_size() * 100;
      // Can't map portions outside the buffer.
      ASSERT_FALSE(buffer->MapAtCpuAddr(addr, magma::page_size(), magma::page_size() * 2));
    }

    uint64_t minimum_address = magma::PlatformBuffer::MinimumMappableAddress();
    uint64_t address_region_length = magma::PlatformBuffer::MappableAddressRegionLength();

    // This random generator is seeded with a fixed value, so the results should be the same on
    // every run.
    std::mt19937_64 random_generator;
    std::uniform_int_distribution<uint64_t> distribution(
        0, address_region_length / magma::page_size() - 1);

    // The fraction of the address space that's mapped initially should be 1/8th or less (worst case
    // is with ASAN), so the probability of this loop failing 100 times is 2^-300, which should be
    // low enough.
    for (i = 0; i < 100; i++) {
      addr = distribution(random_generator) * magma::page_size() + minimum_address;
      if (buffer->MapAtCpuAddr(addr, 0, magma::page_size()))
        break;
    }

    ASSERT_LT(i, 100u);
    EXPECT_EQ(0u, *reinterpret_cast<uint64_t*>(addr));
    void* new_addr;
    EXPECT_TRUE(buffer->MapCpu(&new_addr));
    EXPECT_EQ(reinterpret_cast<uint64_t>(new_addr), addr);

    // Should fail, because it's already mapped.
    for (i = 0; i < 100; i++) {
      addr += magma::page_size() * 100;
      if (buffer->MapAtCpuAddr(addr, 0, magma::page_size()))
        break;
    }
    EXPECT_EQ(100u, i);
    EXPECT_TRUE(buffer->UnmapCpu());
    EXPECT_TRUE(buffer->UnmapCpu());
    for (i = 0; i < 100; i++) {
      addr = distribution(random_generator) * magma::page_size() + minimum_address;
      if (buffer->MapAtCpuAddr(addr, 0, magma::page_size()))
        break;
    }

    EXPECT_LT(i, 100u);
  }

  static void MapConstrained() {
    const uint64_t kPageSize = magma::page_size();
    const uint64_t kLength = kPageSize * 2;
    const uint64_t kDefaultAlignment = 0;
    const uint64_t kNoLimit = std::numeric_limits<uint64_t>::max();
    const uint64_t k4GLimit = uint64_t{1} << 32;
    void* va_out;

    std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(kLength, "test");

    // Test argument validation.
    EXPECT_FALSE(buffer->MapCpuConstrained(nullptr, kPageSize, kNoLimit, kDefaultAlignment));
    EXPECT_FALSE(
        buffer->MapCpuConstrained(&va_out, kLength + kPageSize, kNoLimit, kDefaultAlignment));
    EXPECT_FALSE(buffer->MapCpuConstrained(&va_out, kPageSize + 1, kNoLimit, kDefaultAlignment));
    EXPECT_FALSE(buffer->MapCpuConstrained(&va_out, kPageSize, k4GLimit + 1, kDefaultAlignment));
    EXPECT_FALSE(buffer->MapCpuConstrained(&va_out, kPageSize, k4GLimit, kDefaultAlignment + 1));
    EXPECT_FALSE(buffer->MapCpuConstrained(&va_out, kPageSize, k4GLimit, kPageSize + 1));
    EXPECT_FALSE(buffer->MapCpuConstrained(&va_out, kPageSize, k4GLimit, kPageSize * 2 + 1));

    // Test basic mapping.
    EXPECT_TRUE(buffer->MapCpuConstrained(&va_out, kLength, k4GLimit, kDefaultAlignment));
    EXPECT_NE(nullptr, va_out);
    EXPECT_LT(reinterpret_cast<uint64_t>(va_out), k4GLimit);
    EXPECT_LE(reinterpret_cast<uint64_t>(va_out) + kLength, k4GLimit);

    // Test map counting.
    void* const original_va = va_out;
    uint32_t i;
    for (i = 0; i < 100; i++) {
      ASSERT_TRUE(buffer->MapCpuConstrained(&va_out, kLength, k4GLimit, kDefaultAlignment));
      EXPECT_EQ(original_va, va_out);
    }
    EXPECT_EQ(100u, i);
  }

  enum class CreateConfig { kCreate, kImport };

  enum class ParentVmarConfig {
    kNoParentVmar,
    kWithParentVmar,
  };

  static void MapWithFlags(CreateConfig create_config, ParentVmarConfig parent_vmar_config) {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size() * 2, "test");

    if (create_config == CreateConfig::kImport) {
      uint32_t duplicate_handle;
      ASSERT_TRUE(buffer->duplicate_handle(&duplicate_handle));
      buffer = magma::PlatformBuffer::Import(duplicate_handle);
    }

    std::unique_ptr<magma::PlatformBuffer::MappingAddressRange> address_range;

    if (parent_vmar_config == ParentVmarConfig::kWithParentVmar) {
      uint32_t vmar_handle = GetVmarHandle(magma::page_size() * 100);
      uint32_t dupe_vmar_handle;
      ASSERT_TRUE(magma::PlatformHandle::duplicate_handle(vmar_handle, &dupe_vmar_handle));

      buffer->SetMappingAddressRange(magma::PlatformBuffer::MappingAddressRange::Create(
          magma::PlatformHandle::Create(vmar_handle)));

      address_range = magma::PlatformBuffer::MappingAddressRange::Create(
          magma::PlatformHandle::Create(dupe_vmar_handle));
    } else {
      address_range = magma::PlatformBuffer::MappingAddressRange::CreateDefault();
    }
    ASSERT_TRUE(address_range);

    std::unique_ptr<magma::PlatformBuffer::Mapping> read_only;
    std::unique_ptr<magma::PlatformBuffer::Mapping> partial;
    std::unique_ptr<magma::PlatformBuffer::Mapping> entire;
    EXPECT_TRUE(buffer->MapCpuWithFlags(0, magma::page_size(), magma::PlatformBuffer::kMapRead,
                                        &read_only));
    EXPECT_TRUE(buffer->MapCpuWithFlags(
        magma::page_size(), magma::page_size(),
        magma::PlatformBuffer::kMapWrite | magma::PlatformBuffer::kMapRead, &partial));
    EXPECT_TRUE(buffer->MapCpuWithFlags(
        0, 2 * magma::page_size(),
        magma::PlatformBuffer::kMapWrite | magma::PlatformBuffer::kMapRead, &entire));

    EXPECT_TRUE(reinterpret_cast<uintptr_t>(read_only->address()) >= address_range->Base() &&
                reinterpret_cast<uintptr_t>(read_only->address()) <
                    address_range->Base() + address_range->Length());
    EXPECT_TRUE(reinterpret_cast<uintptr_t>(partial->address()) >= address_range->Base() &&
                reinterpret_cast<uintptr_t>(partial->address()) <
                    address_range->Base() + address_range->Length());
    EXPECT_TRUE(reinterpret_cast<uintptr_t>(entire->address()) >= address_range->Base() &&
                reinterpret_cast<uintptr_t>(entire->address()) <
                    address_range->Base() + address_range->Length());

    // Try reading/writing at different locations in the partial/full maps.
    uint32_t temp_data = 5;
    memcpy(partial->address(), &temp_data, sizeof(temp_data));
    memcpy(&temp_data, reinterpret_cast<uint8_t*>(entire->address()) + magma::page_size(),
           sizeof(temp_data));
    EXPECT_EQ(5u, temp_data);
    memcpy(entire->address(), &temp_data, sizeof(temp_data));
    memcpy(&temp_data, read_only->address(), sizeof(temp_data));
    EXPECT_EQ(5u, temp_data);

    std::unique_ptr<magma::PlatformBuffer::Mapping> bad;
    // Try mapping with bad offsets or flags.
    EXPECT_FALSE(
        buffer->MapCpuWithFlags(1u, magma::page_size(), magma::PlatformBuffer::kMapRead, &bad));
    EXPECT_FALSE(
        buffer->MapCpuWithFlags(0u, magma::page_size() + 1, magma::PlatformBuffer::kMapRead, &bad));
    EXPECT_FALSE(buffer->MapCpuWithFlags(magma::page_size(), 2 * magma::page_size(),
                                         magma::PlatformBuffer::kMapRead, &bad));
    EXPECT_FALSE(
        buffer->MapCpuWithFlags(0u, magma::page_size(), magma::PlatformBuffer::kMapWrite, &bad));
  }
#endif

  static void CachePolicy() {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test");
    EXPECT_FALSE(buffer->SetCachePolicy(100));

    uint32_t duplicate_handle;
    ASSERT_TRUE(buffer->duplicate_handle(&duplicate_handle));
    std::unique_ptr<magma::PlatformBuffer> buffer1 =
        magma::PlatformBuffer::Import(duplicate_handle);

    EXPECT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_CACHED));
    EXPECT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_WRITE_COMBINING));
    magma_cache_policy_t cache_policy;
    EXPECT_EQ(MAGMA_STATUS_OK, buffer->GetCachePolicy(&cache_policy));
    EXPECT_EQ(static_cast<uint32_t>(MAGMA_CACHE_POLICY_WRITE_COMBINING), cache_policy);
    EXPECT_EQ(MAGMA_STATUS_OK, buffer1->GetCachePolicy(&cache_policy));
    EXPECT_EQ(static_cast<uint32_t>(MAGMA_CACHE_POLICY_WRITE_COMBINING), cache_policy);
    EXPECT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED));
    EXPECT_EQ(MAGMA_STATUS_OK, buffer->GetCachePolicy(&cache_policy));
    EXPECT_EQ(static_cast<uint32_t>(MAGMA_CACHE_POLICY_UNCACHED), cache_policy);
    EXPECT_EQ(MAGMA_STATUS_OK, buffer1->GetCachePolicy(&cache_policy));
    EXPECT_EQ(static_cast<uint32_t>(MAGMA_CACHE_POLICY_UNCACHED), cache_policy);
  }

  static void test_buffer_passing(magma::PlatformBuffer* buf, magma::PlatformBuffer* buf1) {
    EXPECT_EQ(buf1->size(), buf->size());
    EXPECT_EQ(buf1->id(), buf->id());

    std::vector<void*> virt_addr(2);
    EXPECT_TRUE(buf1->MapCpu(&virt_addr[0]));
    EXPECT_TRUE(buf->MapCpu(&virt_addr[1]));

    unsigned int some_offset = buf->size() / 2;
    int old_value =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset);
    int check =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(old_value, check);

    int new_value = old_value + 1;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset) =
        new_value;
    check = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(new_value, check);

    EXPECT_TRUE(buf->UnmapCpu());
  }

  static void BufferPassing() {
    std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

    buffer[0] = magma::PlatformBuffer::Create(1, "test");
    ASSERT_NE(buffer[0], nullptr);
    uint32_t duplicate_handle;
    ASSERT_TRUE(buffer[0]->duplicate_handle(&duplicate_handle));
    buffer[1] = magma::PlatformBuffer::Import(duplicate_handle);
    ASSERT_NE(buffer[1], nullptr);

    EXPECT_EQ(buffer[0]->size(), buffer[1]->size());

    test_buffer_passing(buffer[0].get(), buffer[1].get());

    buffer[0] = std::move(buffer[1]);
    ASSERT_NE(buffer[0], nullptr);
    ASSERT_TRUE(buffer[0]->duplicate_handle(&duplicate_handle));
    buffer[1] = magma::PlatformBuffer::Import(duplicate_handle);
    ASSERT_NE(buffer[1], nullptr);

    EXPECT_EQ(buffer[0]->size(), buffer[1]->size());

    test_buffer_passing(buffer[0].get(), buffer[1].get());
  }

  static void CommitPages(uint32_t num_pages) {
    {
      std::unique_ptr<magma::PlatformBuffer> buffer =
          magma::PlatformBuffer::Create(num_pages * magma::page_size(), "test");

      // start of range invalid
      EXPECT_FALSE(buffer->CommitPages(num_pages, 1));
      // end of range invalid
      EXPECT_FALSE(buffer->CommitPages(0, num_pages + 1));
      // one page in the middle
      EXPECT_TRUE(buffer->CommitPages(num_pages / 2, 1));
      // entire buffer
      EXPECT_TRUE(buffer->CommitPages(0, num_pages));
      // entire buffer again
      EXPECT_TRUE(buffer->CommitPages(0, num_pages));
    }

    {
      const uint64_t kLength = num_pages * magma::page_size();
      void* address;

      std::unique_ptr<magma::PlatformBuffer> buffer =
          magma::PlatformBuffer::Create(kLength, "test");

      // Exercise commit pages on mapped buffer.
      EXPECT_TRUE(buffer->MapCpu(&address, kLength));
      EXPECT_TRUE(buffer->CommitPages(0, num_pages));
    }
  }

  static void MapAligned(uint32_t num_pages) {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(num_pages * magma::page_size(), "test");

    void* address;
    // Alignment not page-aligned.
    EXPECT_FALSE(buffer->MapCpu(&address, 2048));
    // Alignment isn't a power of 2.
    EXPECT_FALSE(buffer->MapCpu(&address, magma::page_size() * 3));

    constexpr uintptr_t kAlignment = (1 << 24);
    EXPECT_TRUE(buffer->MapCpu(&address, kAlignment));
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(address) & (kAlignment - 1));
    EXPECT_TRUE(buffer->UnmapCpu());
  }

  static void CleanCache(bool mapped, bool invalidate) {
    const uint64_t kNumPages = 100;
    const uint64_t kBufferSize = kNumPages * magma::page_size();
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(kBufferSize, "test");
    void* address;
    if (mapped)
      buffer->MapCpu(&address);

    // start of range invalid
    EXPECT_FALSE(buffer->CleanCache(kBufferSize, 1, invalidate));
    // end of range invalid
    EXPECT_FALSE(buffer->CleanCache(0, kBufferSize + 1, invalidate));
    // one byte in the middle
    EXPECT_TRUE(buffer->CleanCache(kBufferSize / 2, 1, invalidate));
    // entire buffer
    EXPECT_TRUE(buffer->CleanCache(0, kBufferSize, invalidate));
    // entire buffer again
    EXPECT_TRUE(buffer->CleanCache(0, kBufferSize, invalidate));
  }

#if defined(__Fuchsia__)
  static void NotMappable() {
    const uint64_t kNumPages = 100;
    const uint64_t kBufferSize = kNumPages * magma::page_size();
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(kBufferSize, "test");
    uint32_t start_handle;
    EXPECT_TRUE(buffer->duplicate_handle(&start_handle));

    constexpr uint32_t kRightsToRemove[] = {0u, ZX_RIGHT_WRITE, ZX_RIGHT_READ, ZX_RIGHT_MAP};

    for (uint32_t right : kRightsToRemove) {
      zx_handle_t try_handle;
      EXPECT_EQ(ZX_OK,
                zx_handle_duplicate(start_handle, ZX_DEFAULT_VMO_RIGHTS & ~right, &try_handle));
      auto try_buffer = magma::PlatformBuffer::Import(try_handle);
      EXPECT_NE(nullptr, try_buffer);
      magma_bool_t is_mappable;
      EXPECT_EQ(MAGMA_STATUS_OK, try_buffer->GetIsMappable(&is_mappable));
      if (right == 0) {
        EXPECT_TRUE(is_mappable);
      } else {
        EXPECT_FALSE(is_mappable);
      }
    }
  }
#endif

  static void CheckAddressRegionSize() {
    auto range = magma::PlatformBuffer::MappingAddressRange::CreateDefault();
#if __x86_64__
    // Almost 1 << 47 - see USER_ASPACE_SIZE.
    EXPECT_EQ(range->Base(), 0x1000000ull);
    EXPECT_EQ(range->Length(), 0x7ffffefff000ull);
#else
    // Assume platform is 64-bit and has 48-bit usermode virtual addresses.
    // A little at the top may be inaccessible - see USER_ASPACE_SIZE.
    EXPECT_GE((1ul << 48), range->Base() + range->Length());
    EXPECT_LE((1ul << 48) - 1024 * 1024 * 1024, range->Base() + range->Length());
#endif
  }

#if defined(__Fuchsia__)
  static void MappingAddressRange() {
    const uint64_t kVmarLength = magma::page_size() * 100;
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test");

    EXPECT_TRUE(buffer->SetMappingAddressRange(
        magma::PlatformBuffer::MappingAddressRange::CreateDefault()));
    EXPECT_TRUE(buffer->SetMappingAddressRange(magma::PlatformBuffer::MappingAddressRange::Create(
        magma::PlatformHandle::Create(GetVmarHandle(kVmarLength)))));
    EXPECT_TRUE(buffer->SetMappingAddressRange(
        magma::PlatformBuffer::MappingAddressRange::CreateDefault()));

    void* virt_addr = nullptr;
    EXPECT_TRUE(buffer->MapCpu(&virt_addr));
    // Can't change it when mapped
    EXPECT_FALSE(buffer->SetMappingAddressRange(magma::PlatformBuffer::MappingAddressRange::Create(
        magma::PlatformHandle::Create(GetVmarHandle(kVmarLength)))));
    // May set to default if already default.
    EXPECT_TRUE(buffer->SetMappingAddressRange(
        magma::PlatformBuffer::MappingAddressRange::CreateDefault()));

    EXPECT_TRUE(buffer->UnmapCpu());
    EXPECT_TRUE(buffer->SetMappingAddressRange(
        magma::PlatformBuffer::MappingAddressRange::CreateDefault()));
    EXPECT_TRUE(buffer->SetMappingAddressRange(magma::PlatformBuffer::MappingAddressRange::Create(
        magma::PlatformHandle::Create(GetVmarHandle(kVmarLength)))));
  }

  static void Padding() {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test");
    EXPECT_FALSE(buffer->SetPadding(1));
    EXPECT_TRUE(buffer->SetPadding(magma::page_size()));

    void* va;
    EXPECT_TRUE(buffer->MapCpuConstrained(&va, buffer->size(), 1ul << 38));

    std::unique_ptr<magma::PlatformBuffer> probe_buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "prove");
    // Check that a buffer can't be mapped immediately after.
    EXPECT_FALSE(probe_buffer->MapAtCpuAddr(reinterpret_cast<uint64_t>(va) + buffer->size(), 0,
                                            probe_buffer->size()));

    EXPECT_TRUE(buffer->UnmapCpu());

    EXPECT_TRUE(buffer->MapCpu(&va));
    EXPECT_FALSE(probe_buffer->MapAtCpuAddr(reinterpret_cast<uint64_t>(va) + buffer->size(), 0,
                                            probe_buffer->size()));
    EXPECT_TRUE(buffer->UnmapCpu());

    // This is an address that probably won't be used by any other allocation, even with the ASAN
    // shadow enabled.
    constexpr uint64_t kMappedAddr = 1ul << 46;
    if (buffer->MapAtCpuAddr(kMappedAddr, 0, buffer->size())) {
      EXPECT_FALSE(
          probe_buffer->MapAtCpuAddr(kMappedAddr + buffer->size(), 0, probe_buffer->size()));
      EXPECT_TRUE(buffer->UnmapCpu());

    } else {
      printf("Warning: MapAtCpuAddr failed, skipping probe test.");
    }
  }
#endif

  static void ReadWrite() {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test");
    constexpr uint32_t kValue = 0xdeadbeef;
    constexpr uint32_t kOffset = 1;
    EXPECT_TRUE(buffer->Write(&kValue, kOffset, sizeof(kValue)));
    EXPECT_FALSE(buffer->Write(&kValue, magma::page_size() - 3, sizeof(kValue)));

    uint32_t value_out;
    EXPECT_FALSE(buffer->Read(&value_out, magma::page_size() - 3, sizeof(value_out)));

    EXPECT_TRUE(buffer->Read(&value_out, kOffset, sizeof(value_out)));
    EXPECT_EQ(kValue, value_out);

    void* virt_addr = nullptr;
    EXPECT_TRUE(buffer->MapCpu(&virt_addr));
    EXPECT_EQ(0, memcmp(&kValue, reinterpret_cast<uint8_t*>(virt_addr) + kOffset, sizeof(kValue)));

    std::unique_ptr<magma::PlatformBuffer> wc_buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test-wc");
    EXPECT_TRUE(wc_buffer->SetCachePolicy(MAGMA_CACHE_POLICY_WRITE_COMBINING));

    // Read and write are expected to fail on write-combining or uncached
    // vmos.
    EXPECT_FALSE(wc_buffer->Write(&kValue, 0, sizeof(kValue)));
    EXPECT_FALSE(wc_buffer->Read(&value_out, 0, sizeof(value_out)));
  }

  static void Children() {
    std::unique_ptr<magma::PlatformBuffer> buffer =
        magma::PlatformBuffer::Create(magma::page_size(), "test");
    ASSERT_TRUE(buffer);

    EXPECT_FALSE(buffer->HasChildren());

    constexpr uint32_t kConstant = 0x1234abcd;
    void* ptr;
    ASSERT_TRUE(buffer->MapCpu(&ptr));
    *reinterpret_cast<uint32_t*>(ptr) = kConstant;
    EXPECT_TRUE(buffer->UnmapCpu());

    uint32_t buffer_handle;
    EXPECT_TRUE(buffer->CreateChild(&buffer_handle));
    EXPECT_TRUE(buffer->HasChildren());

    auto child1 = magma::PlatformBuffer::Import(buffer_handle);
    ASSERT_TRUE(child1);

    ASSERT_TRUE(child1->MapCpu(&ptr));
    EXPECT_EQ(kConstant, *reinterpret_cast<uint32_t*>(ptr));
    *reinterpret_cast<uint32_t*>(ptr) = kConstant + 1;
    EXPECT_TRUE(child1->UnmapCpu());

    EXPECT_TRUE(buffer->CreateChild(&buffer_handle));
    EXPECT_TRUE(buffer->HasChildren());

    auto child2 = magma::PlatformBuffer::Import(buffer_handle);
    ASSERT_TRUE(child2);

    ASSERT_TRUE(child2->MapCpu(&ptr));
    EXPECT_EQ(kConstant + 1, *reinterpret_cast<uint32_t*>(ptr));
    EXPECT_TRUE(child2->UnmapCpu());

    child1.reset();
    EXPECT_TRUE(buffer->HasChildren());

    child2.reset();
    EXPECT_FALSE(buffer->HasChildren());
  }
};

TEST(PlatformBuffer, Basic) {
  TestPlatformBuffer::Basic(0);
  TestPlatformBuffer::Basic(1);
  TestPlatformBuffer::Basic(4095);
  TestPlatformBuffer::Basic(4096);
  TestPlatformBuffer::Basic(4097);
  TestPlatformBuffer::Basic(20 * magma::page_size());
  TestPlatformBuffer::Basic(10 * 1024 * 1024);
}

TEST(PlatformBuffer, CachePolicy) { TestPlatformBuffer::CachePolicy(); }

TEST(PlatformBuffer, BufferPassing) { TestPlatformBuffer::BufferPassing(); }

TEST(PlatformBuffer, Commit) {
  TestPlatformBuffer::CommitPages(1);
  TestPlatformBuffer::CommitPages(16);
  TestPlatformBuffer::CommitPages(1024);
}

TEST(PlatformBuffer, MapAligned) {
  TestPlatformBuffer::MapAligned(1);
  TestPlatformBuffer::MapAligned(16);
  TestPlatformBuffer::MapAligned(1024);
}

TEST(PlatformBuffer, CleanCache) {
  TestPlatformBuffer::CleanCache(false, false);
  TestPlatformBuffer::CleanCache(false, true);
}

TEST(PlatformBuffer, CleanCacheMapped) {
  TestPlatformBuffer::CleanCache(true, false);
  TestPlatformBuffer::CleanCache(true, true);
}

TEST(PlatformBuffer, ReadWrite) { TestPlatformBuffer::ReadWrite(); }

TEST(PlatformBuffer, Children) { TestPlatformBuffer::Children(); }

#if defined(__Fuchsia__)

TEST(PlatformBuffer, CreateAndMapWithFlags) {
  TestPlatformBuffer::MapWithFlags(TestPlatformBuffer::CreateConfig::kCreate,
                                   TestPlatformBuffer::ParentVmarConfig::kNoParentVmar);
}
TEST(PlatformBuffer, ImportAndMapWithFlags) {
  TestPlatformBuffer::MapWithFlags(TestPlatformBuffer::CreateConfig::kImport,
                                   TestPlatformBuffer::ParentVmarConfig::kNoParentVmar);
}
TEST(PlatformBuffer_ParentVmar, CreateAndMapWithFlags) {
  TestPlatformBuffer::MapWithFlags(TestPlatformBuffer::CreateConfig::kCreate,
                                   TestPlatformBuffer::ParentVmarConfig::kWithParentVmar);
}
TEST(PlatformBuffer_ParentVmar, ImportAndMapWithFlags) {
  TestPlatformBuffer::MapWithFlags(TestPlatformBuffer::CreateConfig::kImport,
                                   TestPlatformBuffer::ParentVmarConfig::kWithParentVmar);
}

TEST(PlatformBuffer, NotMappable) { TestPlatformBuffer::NotMappable(); }

TEST(PlatformBuffer, AddressRegionSize) { TestPlatformBuffer::CheckAddressRegionSize(); }

TEST(PlatformBuffer, MappingAddressRange) { TestPlatformBuffer::MappingAddressRange(); }

TEST(PlatformBuffer, MapSpecific) { TestPlatformBuffer::MapSpecific(); }

// TODO(fxbug.dev/57091)
#if !defined(HAS_FEATURE_ASAN)
TEST(PlatformBuffer, MapConstrained) { TestPlatformBuffer::MapConstrained(); }

TEST(PlatformBuffer, Padding) { TestPlatformBuffer::Padding(); }
#endif

#endif
