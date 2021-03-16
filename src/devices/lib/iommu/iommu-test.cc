// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/iommu/iommu.h"

#include <lib/ddk/debug.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace x86 {

// These structs provide us convenient ways to declare the salient pieces of the dmar tables in the
// test code.
struct Scope {
  uint8_t start_bus;
  std::vector<uint8_t> hops;
};

struct MemData {
  uint64_t base_addr;
  uint64_t len;
};

struct ReservedMem {
  MemData data;
  std::vector<Scope> scopes;
};

struct DescData {
  uint64_t base;
  uint16_t segment;
  bool whole;
};

struct Desc {
  DescData data;
  std::vector<Scope> scopes;
  std::vector<ReservedMem> reserved_memory;
};

class DmarBuilder {
 public:
  DmarBuilder() { push((ACPI_TABLE_DMAR){}); }
  void AddHardwareUnit(const DescData &desc, std::vector<Scope> &&scopes) {
    size_t offset = build_.size();
    AddDesc(desc);
    for (const auto &scope : scopes) {
      AddScope(scope);
    }
    reinterpret_cast<ACPI_DMAR_HARDWARE_UNIT *>(build_.data() + offset)->Header.Length =
        static_cast<uint16_t>(build_.size() - offset);
  }
  void AddDesc(const DescData &desc) {
    ACPI_DMAR_HARDWARE_UNIT unit;
    unit.Header.Type = ACPI_DMAR_TYPE_HARDWARE_UNIT;
    unit.Header.Length = 0;
    unit.Flags = desc.whole ? ACPI_DMAR_INCLUDE_ALL : 0;
    unit.Segment = desc.segment;
    unit.Address = desc.base;
    push(unit);
  }
  void AddScope(const Scope &scope) {
    ACPI_DMAR_DEVICE_SCOPE device_scope;
    device_scope.EntryType = ACPI_DMAR_SCOPE_TYPE_ENDPOINT;
    device_scope.Length =
        static_cast<uint8_t>(sizeof(device_scope) + scope.hops.size() * sizeof(uint16_t));
    device_scope.Bus = scope.start_bus;
    push(device_scope);
    for (auto &hop : scope.hops) {
      uint8_t dev = static_cast<uint8_t>(hop >> 3);
      uint8_t func = hop & 0b111;
      uint16_t v =
          static_cast<uint16_t>((static_cast<uint16_t>(func) << 8u) | static_cast<uint16_t>(dev));
      push(v);
    }
  }
  void AddReservedMemory(const MemData &data, uint16_t pci_segment, std::vector<Scope> &&scopes) {
    ACPI_DMAR_RESERVED_MEMORY mem;
    mem.Header.Type = ACPI_DMAR_TYPE_RESERVED_MEMORY;
    mem.Header.Length = 0;
    mem.Segment = pci_segment;
    mem.BaseAddress = data.base_addr;
    mem.EndAddress = data.base_addr + data.len - 1;
    size_t offset = build_.size();
    push(mem);
    for (const auto &scope : scopes) {
      AddScope(scope);
    }
    reinterpret_cast<ACPI_DMAR_RESERVED_MEMORY *>(build_.data() + offset)->Header.Length =
        static_cast<uint16_t>(build_.size() - offset);
  }
  template <typename T>
  void push(const T &object) {
    size_t current = build_.size();
    build_.resize(current + sizeof(T));
    memcpy(build_.data() + current, &object, sizeof(T));
  }
  // Fills in the length of the dmar table and switches to the built_ vector, which will provided
  // a word aligned allocation of the table.
  const ACPI_TABLE_DMAR *Build() {
    uint32_t length = static_cast<uint32_t>(build_.size());
    built_.resize((length + 7) / 8);
    memcpy(built_.data(), build_.data(), length);
    build_.clear();
    ACPI_TABLE_DMAR *dmar = reinterpret_cast<ACPI_TABLE_DMAR *>(built_.data());
    dmar->Header.Length = length;
    return dmar;
  }

 private:
  std::vector<uint8_t> build_;
  std::vector<uint64_t> built_;
};

class IommuTest : public zxtest::Test {
 public:
  IommuTest() = default;
  ~IommuTest() = default;

 protected:
  void CompareScope(const zx_iommu_desc_intel_scope_t &scope, const Scope &expected) {
    EXPECT_EQ(scope.type, ZX_IOMMU_INTEL_SCOPE_ENDPOINT);
    EXPECT_EQ(scope.start_bus, expected.start_bus);
    ASSERT_EQ(scope.num_hops, expected.hops.size());
    for (uint8_t i = 0; i < scope.num_hops; i++) {
      EXPECT_EQ(scope.dev_func[i], expected.hops[i]);
    }
  }
  void CompareScopes(fbl::Span<zx_iommu_desc_intel_scope_t> scopes,
                     const std::vector<Scope> &expected) {
    EXPECT_EQ(scopes.size(), expected.size());
    auto it1 = scopes.begin();
    auto it2 = expected.begin();
    for (; it1 != scopes.end() && it2 != expected.end(); it1++, it2++) {
      CompareScope(*it1, *it2);
    }
  }
  void CompareDesc(IommuDesc &desc, const Desc &expected) {
    zx_iommu_desc_intel_t *raw_desc = desc.RawDesc();
    EXPECT_EQ(raw_desc->register_base, expected.data.base);
    EXPECT_EQ(raw_desc->pci_segment, expected.data.segment);
    EXPECT_EQ(raw_desc->whole_segment, expected.data.whole);

    CompareScopes(desc.Scopes(), expected.scopes);

    auto reserved_mem = desc.ReservedMem();
    auto it = expected.reserved_memory.begin();
    for (; it != expected.reserved_memory.end(); it++) {
      ASSERT_GE(reserved_mem.size(), sizeof(zx_iommu_desc_intel_reserved_memory));
      zx_iommu_desc_intel_reserved_memory *mem =
          reinterpret_cast<zx_iommu_desc_intel_reserved_memory *>(reserved_mem.data());
      EXPECT_EQ(mem->base_addr, it->data.base_addr);
      EXPECT_EQ(mem->len, it->data.len);

      ASSERT_GE(reserved_mem.size(),
                sizeof(zx_iommu_desc_intel_reserved_memory) + mem->scope_bytes);
      auto scope_span =
          reserved_mem.subspan(sizeof(zx_iommu_desc_intel_reserved_memory), mem->scope_bytes);
      auto scopes = fbl::Span<zx_iommu_desc_intel_scope_t>(
          reinterpret_cast<zx_iommu_desc_intel_scope_t *>(scope_span.data()),
          scope_span.size() / sizeof(zx_iommu_desc_intel_scope_t));
      reserved_mem =
          reserved_mem.subspan(sizeof(zx_iommu_desc_intel_reserved_memory) + mem->scope_bytes);

      CompareScopes(scopes, it->scopes);
    }
    EXPECT_EQ(it, expected.reserved_memory.end());
    EXPECT_EQ(reserved_mem.size(), 0);
  }
  void RunTest(const ACPI_TABLE_DMAR *dmar, const std::vector<Desc> &expected) {
    x86::IommuManager man([](fx_log_severity_t severity, const char *file, int line,
                             const char *msg,
                             va_list args) { zxlogvf_etc(severity, file, line, msg, args); });
    ASSERT_OK(man.InitDesc(dmar));
    auto it1 = man.iommus_.begin();
    auto it2 = expected.begin();
    for (; it1 != man.iommus_.end() && it2 != expected.end(); it1++, it2++) {
      CompareDesc(*it1, *it2);
    }
    EXPECT_EQ(it1, man.iommus_.end());
    EXPECT_EQ(it2, expected.end());
  }
};

TEST_F(IommuTest, NoIommus) {
  DmarBuilder dmar;
  // Expect an empty set of descriptors.
  std::vector<Desc> expected = {};
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, SimpleWholeSegment) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, true};
  dmar.AddHardwareUnit(hu0, {});
  std::vector<Desc> expected = {
      // Expect no scopes or regions.
      {hu0, {}, {}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, SimplePartialSegment) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, false};
  const Scope scope0 = {0x00, {0x1F}};
  dmar.AddHardwareUnit(hu0, {scope0});
  std::vector<Desc> expected = {
      // Expect our single scope.
      {hu0, {scope0}, {}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, WholeSegmentScopes) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, false};
  const Scope scope0 = {0x00, {0x1F}};
  const Scope scope1 = {0x01, {0xA0}};
  const DescData hu1 = {0x00000000FEDA1000, 0x0001, false};
  const Scope scope2 = {0x00, {0xB0}};
  const Scope scope3 = {0x02, {0x0F}};
  const DescData hu2 = {0x00000000FEDA2000, 0x0001, true};
  dmar.AddHardwareUnit(hu0, {scope0, scope1});
  dmar.AddHardwareUnit(hu1, {scope2, scope3});
  dmar.AddHardwareUnit(hu2, {});
  std::vector<Desc> expected = {
      {hu0, {scope0, scope1}, {}},
      {hu1, {scope2, scope3}, {}},
      // Whole scope of unit 2 should only pick up the scopes from matching pci_segment of unit 1
      {hu2, {scope2, scope3}, {}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, WholeSegmentReservedRegion) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, true};
  const DescData hu1 = {0x00000000FEDA1000, 0x0001, true};
  const Scope scope0 = {0x00, {0x1f}};
  dmar.AddHardwareUnit(hu0, {scope0});
  dmar.AddHardwareUnit(hu1, {});
  const MemData mem0 = {0x00000000ADB00000, 0x2000};
  const Scope scope1 = {0x01, {0x0F}};
  dmar.AddReservedMemory(mem0, 0x0000, {scope0, scope1});
  std::vector<Desc> expected = {
      // hu0 is whole segment so it should pick up the  non matching scope as being reserved
      {hu0, {scope0}, {{mem0, {scope1}}}},
      // hu1 is a different pci segment and should not have any reserved mem
      {hu1, {}, {}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, PartialSegmentReservedRegion) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, false};
  const Scope scope0 = {0x00, {0x1f}};
  const Scope scope1 = {0x02, {0xA0}};
  dmar.AddHardwareUnit(hu0, {scope0, scope1});
  const MemData mem0 = {0x00000000ADB00000, 0x2000};
  const Scope scope2 = {0x01, {0x0f}};
  const Scope scope3 = {0x00, {0xB0}};
  dmar.AddReservedMemory(mem0, 0x0000, {scope0, scope2, scope3});
  std::vector<Desc> expected = {
      // only the single matching scope should have been included
      {hu0, {scope0, scope1}, {{mem0, {scope0}}}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

TEST_F(IommuTest, NoMatchingReservedScopes) {
  DmarBuilder dmar;
  const DescData hu0 = {0x00000000FEDA0000, 0x0000, false};
  const Scope scope0 = {0x00, {0x1f}};
  dmar.AddHardwareUnit(hu0, {scope0});
  const MemData mem0 = {0x00000000ADB00000, 0x2000};
  const Scope scope1 = {0x02, {0xA0}};
  dmar.AddReservedMemory(mem0, 0x0000, {scope1});
  std::vector<Desc> expected = {
      // Although a reserved memory entry exists for the pci_segment, with no matching scopes
      // we should end up with no reserved memory at all.
      {hu0, {scope0}, {}},
  };
  ASSERT_NO_FATAL_FAILURES(RunTest(dmar.Build(), expected));
}

}  // namespace x86
