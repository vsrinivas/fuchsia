// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.driver.coding/cpp/driver/fidl.h>

#include <zxtest/zxtest.h>

TEST(WireToNaturalConversion, ClientEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(
                                   fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>())),
                               fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>>);
  EXPECT_EQ(fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>(),
            fidl::ToNatural(fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>()));
}

TEST(NaturalToWireConversion, ClientEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(
                                   fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>())),
                               fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>>);
  fidl::Arena arena;
  EXPECT_EQ(fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>(),
            fidl::ToWire(arena, fdf::ClientEnd<test_driver_coding::DriverChannelProtocol>()));
}

TEST(WireToNaturalConversion, ServerEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(
                                   fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>())),
                               fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>>);
  EXPECT_EQ(fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>(),
            fidl::ToNatural(fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>()));
}

TEST(NaturalToWireConversion, ServerEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(
                                   fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>())),
                               fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>>);
  fidl::Arena arena;
  EXPECT_EQ(fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>(),
            fidl::ToWire(arena, fdf::ServerEnd<test_driver_coding::DriverChannelProtocol>()));
}
