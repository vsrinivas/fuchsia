// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/parser.h"

#include <variant>

#include <gtest/gtest.h>

namespace goldfish::sensor {
namespace {

TEST(Numeric, Numeric) {
  int64_t kSmallInt64 = -1234567;
  Numeric s(kSmallInt64);
  ASSERT_TRUE(s.IsInt());
  ASSERT_EQ(s.Int(), kSmallInt64);
  ASSERT_EQ(s.Double(), static_cast<double>(kSmallInt64));

  double kDouble = 123.456;
  Numeric d(kDouble);
  ASSERT_TRUE(d.IsDouble());
  ASSERT_EQ(d.Double(), kDouble);
  ASSERT_EQ(d.Int(), 123);

  // Large integers that exceeds the double limit are stored as int64_t so
  // it keeps the precision.
  int64_t kMaxInt64 = 9223372036854775807L;
  Numeric i(kMaxInt64);
  ASSERT_TRUE(i.IsInt());
  ASSERT_EQ(i.Int(), kMaxInt64);
}

TEST(Parser, Numeric) {
  const char* kIn1 = "sensor:0.123";
  // Without trailing '\0'
  {
    auto result = ParseSensorReport(kIn1, strlen(kIn1));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    auto val = std::get<Numeric>(result.data[0]);
    ASSERT_TRUE(val.IsDouble());
    EXPECT_EQ(val.Double(), 0.123);
  }

  // With trailing '\0'
  {
    auto result = ParseSensorReport(kIn1, strlen(kIn1) + 1);
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    auto val = std::get<Numeric>(result.data[0]);
    ASSERT_TRUE(val.IsDouble());
    EXPECT_EQ(val.Double(), 0.123);
  }

  // Integer
  const char* kIn2 = "sensor:1234";
  {
    auto result = ParseSensorReport(kIn2, strlen(kIn2));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    auto val = std::get<Numeric>(result.data[0]);
    ASSERT_TRUE(val.IsInt());
    EXPECT_EQ(val.Int(), 1234);
  }

  // Hexadecimal value
  const char* kIn3 = "sensor:0x901d";
  {
    auto result = ParseSensorReport(kIn3, strlen(kIn3));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    auto val = std::get<Numeric>(result.data[0]);
    ASSERT_TRUE(val.IsInt());
    EXPECT_EQ(val.Int(), 0x901d);
  }

  // Exceeding max int64 (9,223,372,036,854,775,807)
  const char* kIn4 = "sensor:9223372036854775808";
  {
    auto result = ParseSensorReport(kIn4, strlen(kIn4));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    auto val = std::get<Numeric>(result.data[0]);
    ASSERT_TRUE(val.IsDouble());

    double target_double = 9223372036854775808.0;
    EXPECT_EQ(val.Double(), target_double);
  }
}

TEST(Parser, String) {
  // Non-numeric value
  const char* kIn1 = "sensor:string123";
  {
    auto result = ParseSensorReport(kIn1, strlen(kIn1));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<std::string>(result.data[0]));
    EXPECT_EQ(std::get<std::string>(result.data[0]), "string123");
  }

  // Numeric field with trailing spaces / characters
  const char* kIn2 = "sensor:1234.56 :1";
  {
    auto result = ParseSensorReport(kIn2, strlen(kIn2));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<std::string>(result.data[0]));
    EXPECT_EQ(std::get<std::string>(result.data[0]), "1234.56 ");
  }

  // Invalid double value
  const char* kIn3 = "sensor:1e999";
  {
    auto result = ParseSensorReport(kIn3, strlen(kIn3));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<std::string>(result.data[0]));
    EXPECT_EQ(std::get<std::string>(result.data[0]), "1e999");
  }
}

TEST(Parser, MultiFields) {
  const char* kIn1 = "sensor";
  {
    auto result = ParseSensorReport(kIn1, strlen(kIn1));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 0u);
  }

  const char* kIn2 = "sensor:0.123:0.456";
  {
    auto result = ParseSensorReport(kIn2, strlen(kIn2));
    EXPECT_EQ(result.name, "sensor");
    EXPECT_EQ(result.data.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Numeric>(result.data[0]));
    EXPECT_TRUE(std::holds_alternative<Numeric>(result.data[1]));
  }
}

}  // namespace
}  // namespace goldfish::sensor
