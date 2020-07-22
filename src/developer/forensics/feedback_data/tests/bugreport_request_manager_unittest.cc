// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/bugreport_request_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::Bugreport;
using testing::UnorderedElementsAreArray;

constexpr zx::duration kDelta = zx::sec(5);

Bugreport MakeBugreport() {
  fsl::SizedVmo vmo;
  FX_CHECK(fsl::VmoFromString("bugreport", &vmo)) << "Failed to make vmo";

  Bugreport bugreport;
  bugreport.set_bugreport(Attachment{
      .key = "key",
      .value = std::move(vmo).ToTransport(),
  });

  return bugreport;
}

bool IsSame(const Attachment& bugreport1, const Attachment& bugreport2) {
  return fsl::GetKoid(bugreport1.value.vmo.get()) == fsl::GetKoid(bugreport2.value.vmo.get());
}

class BugreportRequestManagerTest : public UnitTestFixture {
 public:
  BugreportRequestManagerTest()
      : clock_(new timekeeper::TestClock()),
        request_manager_(kDelta, std::unique_ptr<timekeeper::TestClock>(clock_)) {
    clock_->Set(zx::time(0));
  }

 protected:
  timekeeper::TestClock* clock_;
  BugreportRequestManager request_manager_;
};

struct RequestContext {
  std::optional<uint64_t> id;
  bool responded_to{false};
  Bugreport bugreport{};
};

TEST_F(BugreportRequestManagerTest, PoolsByDelta) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Bugreport bugreport) {
    context1.responded_to = true;
    context1.bugreport = std::move(bugreport);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Bugreport bugreport) {
    context2.responded_to = true;
    context2.bugreport = std::move(bugreport);
  });

  // Advance the clock so the next callback will be in a different pool.
  clock_->Set(clock_->Now() + kDelta);

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout, [&context3](Bugreport bugreport) {
    context3.responded_to = true;
    context3.bugreport = std::move(bugreport);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeBugreport());
  request_manager_.Respond(context3.id.value(), MakeBugreport());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.bugreport.has_bugreport());
  ASSERT_TRUE(context2.bugreport.has_bugreport());
  ASSERT_TRUE(context3.bugreport.has_bugreport());

  EXPECT_TRUE(IsSame(context1.bugreport.bugreport(), context2.bugreport.bugreport()));
  EXPECT_FALSE(IsSame(context1.bugreport.bugreport(), context3.bugreport.bugreport()));
}

TEST_F(BugreportRequestManagerTest, PoolsByTimeout) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Bugreport bugreport) {
    context1.responded_to = true;
    context1.bugreport = std::move(bugreport);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Bugreport bugreport) {
    context2.responded_to = true;
    context2.bugreport = std::move(bugreport);
  });

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout + zx::sec(1), [&context3](Bugreport bugreport) {
    context3.responded_to = true;
    context3.bugreport = std::move(bugreport);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeBugreport());
  request_manager_.Respond(context3.id.value(), MakeBugreport());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.bugreport.has_bugreport());
  ASSERT_TRUE(context2.bugreport.has_bugreport());
  ASSERT_TRUE(context3.bugreport.has_bugreport());

  EXPECT_TRUE(IsSame(context1.bugreport.bugreport(), context2.bugreport.bugreport()));
  EXPECT_FALSE(IsSame(context1.bugreport.bugreport(), context3.bugreport.bugreport()));
}

TEST_F(BugreportRequestManagerTest, SetsPoolSizeAnnotation) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Bugreport bugreport) {
    context1.responded_to = true;
    context1.bugreport = std::move(bugreport);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Bugreport bugreport) {
    context2.responded_to = true;
    context2.bugreport = std::move(bugreport);
  });

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout + zx::sec(1), [&context3](Bugreport bugreport) {
    context3.responded_to = true;
    context3.bugreport = std::move(bugreport);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeBugreport());
  request_manager_.Respond(context3.id.value(), MakeBugreport());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.bugreport.has_annotations());
  ASSERT_TRUE(context2.bugreport.has_annotations());
  ASSERT_TRUE(context3.bugreport.has_annotations());

  EXPECT_THAT(context1.bugreport.annotations(),
              UnorderedElementsAreArray({
                  MatchesAnnotation(kAnnotationDebugPoolSize, "2"),
              }));
  EXPECT_THAT(context2.bugreport.annotations(),
              UnorderedElementsAreArray({
                  MatchesAnnotation(kAnnotationDebugPoolSize, "2"),
              }));
  EXPECT_THAT(context3.bugreport.annotations(),
              UnorderedElementsAreArray({
                  MatchesAnnotation(kAnnotationDebugPoolSize, "1"),
              }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
