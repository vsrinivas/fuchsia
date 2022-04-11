// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/acpi.h"

#include <zircon/compiler.h>

#include <zxtest/zxtest.h>

#include "src/devices/board/lib/acpi/test/device.h"
#include "src/devices/board/lib/acpi/test/mock-acpi.h"

TEST(AcpiInitTests, SetApicIrqModeSucceeds) {
  acpi::test::MockAcpi acpi;
  auto device = std::make_unique<acpi::test::Device>("\\");
  uint64_t pic_value = -1;
  device->AddMethodCallback("_PIC", [&pic_value](std::optional<std::vector<ACPI_OBJECT>> args) {
    EXPECT_TRUE(args.has_value());
    EXPECT_EQ(1, args->size());
    auto& param = (*args)[0];
    EXPECT_EQ(ACPI_TYPE_INTEGER, param.Type);
    pic_value = param.Integer.Value;
    return acpi::ok(nullptr);
  });

  acpi.SetDeviceRoot(std::move(device));

  ASSERT_EQ(AE_OK, acpi.SetApicIrqMode().status_value());
  ASSERT_EQ(1, pic_value);
}

TEST(AcpiInitTests, SetApicIrqModeFails) {
  acpi::test::MockAcpi acpi;
  auto device = std::make_unique<acpi::test::Device>("\\");
  acpi.SetDeviceRoot(std::move(device));

  ASSERT_TRUE(acpi.SetApicIrqMode().is_error());
}

TEST(AcpiInitTests, TestDiscoverWakeGpesFadt) {
  // Construct a device topology.
  auto root = std::make_unique<acpi::test::Device>("\\");

  // Create some devices with wake resources.
  auto fadt_gpe = std::make_unique<acpi::test::Device>("FGPE");
  fadt_gpe->AddMethodCallback("_PRW", [](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> objects = {
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 24}}, {}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });
  root->AddChild(std::move(fadt_gpe));

  acpi::test::MockAcpi acpi;
  acpi.SetDeviceRoot(std::move(root));
  ASSERT_EQ(AE_OK, acpi.DiscoverWakeGpes().status_value());

  auto& gpes = acpi.GetWakeGpes();
  ASSERT_EQ(1, gpes.size());

  auto& entry = gpes[0];
  ASSERT_EQ(nullptr, entry.first);
  ASSERT_EQ(24, entry.second);
}

TEST(AcpiInitTests, TestDiscoverWakeGpesBlockDevice) {
  auto root = std::make_unique<acpi::test::Device>("\\");
  auto gpe = std::make_unique<acpi::test::Device>("GPEH");
  gpe->SetHid("ACPI0006");
  ACPI_HANDLE gpe_handle = gpe.get();
  root->AddChild(std::move(gpe));

  auto reference_gpe = std::make_unique<acpi::test::Device>("RGPE");
  reference_gpe->AddMethodCallback("_PRW", [gpe_handle](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> gpe_objects{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_DEVICE,
                                  .Handle = gpe_handle}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 77}}};
    static std::array<ACPI_OBJECT, 2> prw_objects{
        ACPI_OBJECT{.Package = {.Type = ACPI_TYPE_PACKAGE,
                                .Count = gpe_objects.size(),
                                .Elements = gpe_objects.data()}},
        {}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });
  root->AddChild(std::move(reference_gpe));
  acpi::test::MockAcpi acpi;
  acpi.SetDeviceRoot(std::move(root));
  ASSERT_EQ(AE_OK, acpi.DiscoverWakeGpes().status_value());

  auto& gpes = acpi.GetWakeGpes();
  ASSERT_EQ(1, gpes.size());

  auto& entry = gpes[0];
  ASSERT_EQ(gpe_handle, entry.first);
  ASSERT_EQ(77, entry.second);
}

TEST(AcpiInitTests, TestDiscoverWakeGpesNotGpe) {
  auto root = std::make_unique<acpi::test::Device>("\\");
  auto non_gpe = std::make_unique<acpi::test::Device>("BLAH");
  ACPI_HANDLE non_gpe_handle = non_gpe.get();
  auto not_a_gpe = std::make_unique<acpi::test::Device>("NGPE");
  not_a_gpe->AddMethodCallback("_PRW", [non_gpe_handle](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> gpe_objects{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_DEVICE,
                                  .Handle = non_gpe_handle}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 77}}};
    static std::array<ACPI_OBJECT, 2> prw_objects{
        ACPI_OBJECT{.Package = {.Type = ACPI_TYPE_PACKAGE,
                                .Count = gpe_objects.size(),
                                .Elements = gpe_objects.data()}},
        {}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });
  root->AddChild(std::move(not_a_gpe));

  acpi::test::MockAcpi acpi;
  acpi.SetDeviceRoot(std::move(root));
  ASSERT_EQ(AE_OK, acpi.DiscoverWakeGpes().status_value());
  auto& gpes = acpi.GetWakeGpes();
  ASSERT_TRUE(gpes.empty());
}
