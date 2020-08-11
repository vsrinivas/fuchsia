// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/snapshot_request_manager.h"

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
using fuchsia::feedback::Snapshot;
using testing::UnorderedElementsAreArray;

constexpr zx::duration kDelta = zx::sec(5);

Snapshot MakeSnapshot() {
  fsl::SizedVmo vmo;
  FX_CHECK(fsl::VmoFromString("snapshot", &vmo)) << "Failed to make vmo";

  Snapshot snapshot;
  snapshot.set_archive(Attachment{
      .key = "key",
      .value = std::move(vmo).ToTransport(),
  });

  return snapshot;
}

bool IsSame(const Attachment& snapshot1, const Attachment& snapshot2) {
  return fsl::GetKoid(snapshot1.value.vmo.get()) == fsl::GetKoid(snapshot2.value.vmo.get());
}

class SnapshotRequestManagerTest : public UnitTestFixture {
 public:
  SnapshotRequestManagerTest()
      : clock_(new timekeeper::TestClock()),
        request_manager_(kDelta, std::unique_ptr<timekeeper::TestClock>(clock_)) {
    clock_->Set(zx::time(0));
  }

 protected:
  timekeeper::TestClock* clock_;
  SnapshotRequestManager request_manager_;
};

struct RequestContext {
  std::optional<uint64_t> id;
  bool responded_to{false};
  Snapshot snapshot{};
};

TEST_F(SnapshotRequestManagerTest, PoolsByDelta) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Snapshot snapshot) {
    context1.responded_to = true;
    context1.snapshot = std::move(snapshot);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Snapshot snapshot) {
    context2.responded_to = true;
    context2.snapshot = std::move(snapshot);
  });

  // Advance the clock so the next callback will be in a different pool.
  clock_->Set(clock_->Now() + kDelta);

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout, [&context3](Snapshot snapshot) {
    context3.responded_to = true;
    context3.snapshot = std::move(snapshot);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeSnapshot());
  request_manager_.Respond(context3.id.value(), MakeSnapshot());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.snapshot.has_archive());
  ASSERT_TRUE(context2.snapshot.has_archive());
  ASSERT_TRUE(context3.snapshot.has_archive());

  EXPECT_TRUE(IsSame(context1.snapshot.archive(), context2.snapshot.archive()));
  EXPECT_FALSE(IsSame(context1.snapshot.archive(), context3.snapshot.archive()));
}

TEST_F(SnapshotRequestManagerTest, PoolsByTimeout) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Snapshot snapshot) {
    context1.responded_to = true;
    context1.snapshot = std::move(snapshot);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Snapshot snapshot) {
    context2.responded_to = true;
    context2.snapshot = std::move(snapshot);
  });

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout + zx::sec(1), [&context3](Snapshot snapshot) {
    context3.responded_to = true;
    context3.snapshot = std::move(snapshot);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeSnapshot());
  request_manager_.Respond(context3.id.value(), MakeSnapshot());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.snapshot.has_archive());
  ASSERT_TRUE(context2.snapshot.has_archive());
  ASSERT_TRUE(context3.snapshot.has_archive());

  EXPECT_TRUE(IsSame(context1.snapshot.archive(), context2.snapshot.archive()));
  EXPECT_FALSE(IsSame(context1.snapshot.archive(), context3.snapshot.archive()));
}

TEST_F(SnapshotRequestManagerTest, SetsPoolSizeAnnotation) {
  constexpr zx::duration kTimeout(zx::sec(0));

  RequestContext context1;
  context1.id = request_manager_.Manage(kTimeout, [&context1](Snapshot snapshot) {
    context1.responded_to = true;
    context1.snapshot = std::move(snapshot);
  });

  RequestContext context2;
  context2.id = request_manager_.Manage(kTimeout, [&context2](Snapshot snapshot) {
    context2.responded_to = true;
    context2.snapshot = std::move(snapshot);
  });

  RequestContext context3;
  context3.id = request_manager_.Manage(kTimeout + zx::sec(1), [&context3](Snapshot snapshot) {
    context3.responded_to = true;
    context3.snapshot = std::move(snapshot);
  });

  ASSERT_TRUE(context1.id.has_value());
  ASSERT_FALSE(context2.id.has_value());
  ASSERT_TRUE(context3.id.has_value());

  request_manager_.Respond(context1.id.value(), MakeSnapshot());
  request_manager_.Respond(context3.id.value(), MakeSnapshot());

  ASSERT_TRUE(context1.responded_to);
  ASSERT_TRUE(context2.responded_to);
  ASSERT_TRUE(context3.responded_to);

  ASSERT_TRUE(context1.snapshot.has_annotations());
  ASSERT_TRUE(context2.snapshot.has_annotations());
  ASSERT_TRUE(context3.snapshot.has_annotations());

  EXPECT_THAT(context1.snapshot.annotations(), UnorderedElementsAreArray({
                                                   MatchesAnnotation(kAnnotationDebugPoolSize, "2"),
                                               }));
  EXPECT_THAT(context2.snapshot.annotations(), UnorderedElementsAreArray({
                                                   MatchesAnnotation(kAnnotationDebugPoolSize, "2"),
                                               }));
  EXPECT_THAT(context3.snapshot.annotations(), UnorderedElementsAreArray({
                                                   MatchesAnnotation(kAnnotationDebugPoolSize, "1"),
                                               }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
