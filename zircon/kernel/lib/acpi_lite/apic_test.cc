// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/apic.h>
#include <lib/acpi_lite/testing/test_data.h>
#include <lib/acpi_lite/testing/test_util.h>
#include <lib/zx/status.h>

#include <memory>

#include <gtest/gtest.h>

namespace acpi_lite::testing {
namespace {

// EXPECT_EQ with support for packed fields.
//
// The EXPECT_EQ implementation takes a reference to its arguments, which can
// trigger undefined behaviour for packed fields that are not at the correct
// alignment for their type.
//
// This macro unpacks its arguments first, ensuring that arguments to
// EXPECT_EQ are correctly aligned.
#define EXPECT_EQ_PACKED(a, b) \
  do {                         \
    decltype(a) _a = a;        \
    decltype(b) _b = b;        \
    EXPECT_EQ(_a, _b);         \
  } while (false)

// Collect all items presented in the given callback.
template <typename T>
struct Collector {
 public:
  // Return a callback that, when called, collects the given item into |items|.
  fbl::Function<zx_status_t(const T&)> callback() {
    return [this](const T& item) {
      fbl::AllocChecker ac;
      items.push_back(item, &ac);
      ZX_ASSERT(ac.check());
      return ZX_OK;
    };
  }

  fbl::Vector<T> items;
};

TEST(Apic, EnumerateEveCpus) {
  Collector<acpi_lite::AcpiMadtLocalApicEntry> collector;
  EXPECT_EQ(ZX_OK, acpi_lite::EnumerateProcessorLocalApics(PixelbookEveAcpiParser(),
                                                           collector.callback()));

  // Expect 4 CPUs.
  ASSERT_EQ(collector.items.size(), 4u);
  EXPECT_EQ_PACKED(collector.items[0].apic_id, 0u);
  EXPECT_EQ_PACKED(collector.items[1].apic_id, 1u);
  EXPECT_EQ_PACKED(collector.items[2].apic_id, 2u);
  EXPECT_EQ_PACKED(collector.items[3].apic_id, 3u);
}

TEST(Apic, EnumerateZ840Cpus) {
  Collector<acpi_lite::AcpiMadtLocalApicEntry> collector;
  EXPECT_EQ(ZX_OK, acpi_lite::EnumerateProcessorLocalApics(Z840AcpiParser(), collector.callback()));

  // We check the numcpus to ensure basic parsing is working.
  EXPECT_EQ(56u, collector.items.size());
}

TEST(Apic, EnumerateEveIO) {
  Collector<acpi_lite::AcpiMadtIoApicEntry> collector;
  EXPECT_EQ(ZX_OK, acpi_lite::EnumerateIoApics(PixelbookEveAcpiParser(), collector.callback()));

  ASSERT_EQ(1u, collector.items.size());
  EXPECT_EQ_PACKED(2u, collector.items[0].io_apic_id);
  EXPECT_EQ_PACKED(0u, collector.items[0].global_system_interrupt_base);
  EXPECT_EQ_PACKED(0xFEC00000u, collector.items[0].io_apic_address);
}

TEST(Apic, EnumerateInterruptOverrides) {
  Collector<acpi_lite::AcpiMadtIntSourceOverrideEntry> collector;
  EXPECT_EQ(ZX_OK,
            acpi_lite::EnumerateIoApicIsaOverrides(PixelbookEveAcpiParser(), collector.callback()));

  ASSERT_EQ(2u, collector.items.size());

  EXPECT_EQ_PACKED(0u, collector.items[0].source);
  EXPECT_EQ_PACKED(0u, collector.items[0].flags);
  EXPECT_EQ_PACKED(2u, collector.items[0].global_sys_interrupt);

  EXPECT_EQ_PACKED(9u, collector.items[1].source);
  EXPECT_EQ_PACKED(ACPI_MADT_FLAG_TRIGGER_MASK | ACPI_MADT_FLAG_POLARITY_HIGH,
                   collector.items[1].flags);
  EXPECT_EQ_PACKED(9u, collector.items[1].global_sys_interrupt);
}

}  // namespace
}  // namespace acpi_lite::testing
