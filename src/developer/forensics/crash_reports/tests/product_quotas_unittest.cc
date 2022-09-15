// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/product_quotas.h"

#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/utc_clock_ready_watcher.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::crash_reports {
namespace {

using testing::HasSubstr;

constexpr char kJsonName[] = "product_quotas.json";
constexpr uint64_t kDefaultQuota = 5;
constexpr zx::duration kNegativeResetOffset = zx::min(-10);
constexpr zx::duration kPositiveResetOffset = zx::min(10);

class ProductQuotasTest : public UnitTestFixture {
 public:
  ProductQuotasTest() : clock_(dispatcher()) {}

  void SetUp() override { MakeNewProductQuotas(kDefaultQuota); }

 protected:
  void StartClock() { utc_clock_ready_watcher_.StartClock(); }

  std::string QuotasJsonPath() { return files::JoinPath(tmp_dir_.path(), kJsonName); }

  std::string ReadQuotasJson() {
    std::string json;
    files::ReadFileToString(QuotasJsonPath(), &json);
    return json;
  }

  void MakeNewProductQuotas(std::optional<uint64_t> quota, zx::duration reset_offset = zx::min(0)) {
    product_quotas_ = std::make_unique<ProductQuotas>(
        dispatcher(), &clock_, quota, QuotasJsonPath(), &utc_clock_ready_watcher_, reset_offset);
  }

  std::unique_ptr<ProductQuotas> product_quotas_;

 private:
  timekeeper::AsyncTestClock clock_;
  stubs::UtcClockReadyWatcher utc_clock_ready_watcher_;
  files::ScopedTempDir tmp_dir_;
};

using ProductQuotasDeathTest = ProductQuotasTest;

TEST_F(ProductQuotasTest, HasQuotaRemaining_InsertsProduct) {
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name-some version": 5
    }
})");
}

TEST_F(ProductQuotasTest, HasQuotaRemaining_InsertsProductWithoutVersion) {
  const Product product{
      .name = "some name",
      .version = Error::kMissingValue,
      .channel = Error::kMissingValue,
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name": 5
    }
})");
}

TEST_F(ProductQuotasTest, HasQuotaRemaining_Unlimited) {
  MakeNewProductQuotas(std::nullopt);

  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
  EXPECT_FALSE(files::IsFile(QuotasJsonPath()));
}

TEST_F(ProductQuotasTest, DecrementRemainingQuota) {
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  // Query for product to get it inserted first
  product_quotas_->HasQuotaRemaining(product);
  product_quotas_->DecrementRemainingQuota(product);

  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name-some version": 4
    }
})");
}

TEST_F(ProductQuotasTest, DecrementRemainingQuota_Unlimited) {
  MakeNewProductQuotas(std::nullopt);

  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  // Query for product to get it (potentially) inserted first
  product_quotas_->HasQuotaRemaining(product);
  product_quotas_->DecrementRemainingQuota(product);

  EXPECT_FALSE(files::IsFile(QuotasJsonPath()));
}

TEST_F(ProductQuotasDeathTest, DecrementRemainingQuota) {
  MakeNewProductQuotas(1);
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
  product_quotas_->DecrementRemainingQuota(product);
  ASSERT_DEATH({ product_quotas_->DecrementRemainingQuota(product); },
               HasSubstr("Check failed: remaining_quotas_[key] > 0"));
}

TEST_F(ProductQuotasTest, NoQuotaRemaining) {
  MakeNewProductQuotas(1);
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name-some version": 0
    }
})");
}

TEST_F(ProductQuotasTest, ReinitializesFromJson) {
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  const Product another_product{
      .name = "another name",
      .version = "another version",
      .channel = "another channel",
  };

  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(another_product));
  product_quotas_->DecrementRemainingQuota(another_product);

  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name-some version": 5,
        "another name-another version": 4
    }
})");

  MakeNewProductQuotas(kDefaultQuota);
  product_quotas_->DecrementRemainingQuota(another_product);

  EXPECT_EQ(ReadQuotasJson(), R"({
    "quotas": {
        "some name-some version": 5,
        "another name-another version": 3
    }
})");
}

TEST_F(ProductQuotasTest, NoQuota_DeletesJson) {
  ASSERT_TRUE(files::WriteFile(QuotasJsonPath(), "Test data"));
  ASSERT_FALSE(ReadQuotasJson().empty());

  MakeNewProductQuotas(std::nullopt);

  EXPECT_FALSE(files::IsFile(QuotasJsonPath()));
}

TEST_F(ProductQuotasTest, InsertTimeIntoJson) {
  StartClock();

  // 259200000000000 is January 04 1970 00:00:00, which is the next midnight after the starting
  // point for MonotonicTestClockBase.
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 259200000000000
})");
}

TEST_F(ProductQuotasTest, Clock_NeverStarts) {
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };
  MakeNewProductQuotas(1);
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  RunLoopFor(zx::hour(12));

  // Make new ProductQuota to force it to start a new reset period because no UTC time was persisted
  // in JSON.
  MakeNewProductQuotas(1);

  // Run loop past the UTC deadline without starting the clock.
  RunLoopFor(zx::hour(13));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour reset.
  RunLoopFor(zx::hour(12));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour reset again.
  RunLoopFor(zx::hour(25));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
}

TEST_F(ProductQuotasTest, Clock_StartBeforeDeadlineWithNegativeOffset) {
  // 259200000000000 is January 04 1970 00:00:00
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 03 1970 23:50:00
  const std::string json = R"({
    "next_reset_time_utc_nanos": 259200000000000,
    "quotas": {
        "some name-some version": 0
    }
  })";
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };
  ASSERT_TRUE(files::WriteFile(QuotasJsonPath(), json));

  MakeNewProductQuotas(1, kNegativeResetOffset);
  StartClock();
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Clock time: January 03 1970 23:50:52
  RunLoopFor(zx::hour(18) + zx::min(36));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota.
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour fallback (which should have been cancelled).
  // Clock time: January 04 1970 11:50:52
  RunLoopFor(zx::hour(12));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the new UTC deadline.
  // Clock time: January 04 1970 23:50:52
  RunLoopFor(zx::hour(12));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
}

TEST_F(ProductQuotasTest, Clock_StartBeforeDeadlineWithPositiveOffset) {
  // 259200000000000 is January 04 1970 00:00:00
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 04 1970 00:10:00
  const std::string json = R"({
    "next_reset_time_utc_nanos": 259200000000000,
    "quotas": {
        "some name-some version": 0
    }
  })";
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };
  ASSERT_TRUE(files::WriteFile(QuotasJsonPath(), json));

  MakeNewProductQuotas(1, kPositiveResetOffset);
  StartClock();
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Clock time: January 04 1970 00:10:52
  RunLoopFor(zx::hour(18) + zx::min(56));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour fallback (which should have been cancelled).
  // Clock time: January 04 1970 12:50:52
  RunLoopFor(zx::hour(12));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the new UTC deadline.
  // Clock time: January 05 1970 00:10:52
  RunLoopFor(zx::hour(12));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
}

TEST_F(ProductQuotasTest, Clock_StartAfterDeadlineWithNegativeOffset) {
  // 259200000000000 is January 04 1970 00:00:00
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 03 1970 23:50:00
  const std::string json = R"({
    "next_reset_time_utc_nanos": 259200000000000,
    "quotas": {
        "some name-some version": 0
    }
  })";
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };
  ASSERT_TRUE(files::WriteFile(QuotasJsonPath(), json));

  MakeNewProductQuotas(1, kNegativeResetOffset);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the UTC deadline without starting the clock.
  // Clock Time: January 03 1970 23:50:52
  RunLoopFor(zx::hour(18) + zx::min(36));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  StartClock();
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour fallback (which should have been cancelled).
  // Clock time: January 04 1970 11:50:52
  RunLoopFor(zx::hour(12));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the new UTC deadline.
  // Clock time: January 04 1970 23:10:52
  RunLoopFor(zx::hour(12));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
}

TEST_F(ProductQuotasTest, Clock_StartAfterDeadlineWithPositiveOffset) {
  // 259200000000000 is January 04 1970 00:00:00
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 04 1970 00:50:00
  const std::string json = R"({
    "next_reset_time_utc_nanos": 259200000000000,
    "quotas": {
        "some name-some version": 0
    }
  })";
  const Product product{
      .name = "some name",
      .version = "some version",
      .channel = "some channel",
  };
  ASSERT_TRUE(files::WriteFile(QuotasJsonPath(), json));

  MakeNewProductQuotas(1, kNegativeResetOffset);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the UTC deadline without starting the clock.
  // Clock Time: January 04 1970 00:50:52
  RunLoopFor(zx::hour(18) + zx::min(56));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  StartClock();
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));

  // Exhaust quota
  product_quotas_->DecrementRemainingQuota(product);
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the 24 hour fallback (which should have been cancelled).
  // Clock time: January 04 1970 12:50:52
  RunLoopFor(zx::hour(12));
  EXPECT_FALSE(product_quotas_->HasQuotaRemaining(product));

  // Run loop past the new UTC deadline.
  // Clock time: January 05 1970 00:10:52
  RunLoopFor(zx::hour(12));
  EXPECT_TRUE(product_quotas_->HasQuotaRemaining(product));
}

TEST_F(ProductQuotasTest, ResetAtMidnightWithNegativeOffset) {
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 03 1970 23:50:00
  MakeNewProductQuotas(1, kNegativeResetOffset);
  StartClock();

  // Clock Time: January 03 1970 23:45:52
  RunLoopFor(zx::hour(18) + zx::min(31));

  // 259200000000000 is January 04 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 259200000000000
})");

  // Clock Time: January 03 1970 23:50:52
  RunLoopFor(zx::min(5));

  // 345600000000000 is January 05 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 345600000000000
})");
}

TEST_F(ProductQuotasTest, ResetAtMidnightWithPositiveOffset) {
  // AsyncTestClock starting point is 191692000000000, January 03 1970 05:14:52
  // Reset should be executed on January 04 1970 00:10:00
  MakeNewProductQuotas(1, kPositiveResetOffset);
  StartClock();

  // Clock Time: January 04 1970 00:05:52
  RunLoopFor(zx::hour(18) + zx::min(51));

  // 259200000000000 is January 04 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 259200000000000
})");

  // Clock Time: January 04 1970 00:10:52
  RunLoopFor(zx::min(5));

  // 345600000000000 is January 05 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 345600000000000
})");
}

TEST_F(ProductQuotasTest, TimeFromJson) {
  StartClock();

  // 259200000000000 is January 04 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 259200000000000
})");

  // Make new ProductQuotas to force it to read from JSON.
  MakeNewProductQuotas(5);
  RunLoopFor(zx::hour(25));

  // 345600000000000 is January 05 1970 00:00:00
  EXPECT_EQ(ReadQuotasJson(), R"({
    "next_reset_time_utc_nanos": 345600000000000
})");
}

}  // namespace
}  // namespace forensics::crash_reports
