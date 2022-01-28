// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {
namespace {

TEST(AnnotationManagerTest, SynchronousStaticAnnotations) {
  const Annotations annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  {
    AnnotationManager mananger(annotations);

    EXPECT_EQ(mananger.ImmediatelyAvailable(), annotations);
  }

  {
    AnnotationManager mananger;
    mananger.InsertStatic(annotations);

    EXPECT_EQ(mananger.ImmediatelyAvailable(), annotations);
  }
}

TEST(AnnotationManagerTest, UniqueKeys) {
  ASSERT_DEATH(
      {
        AnnotationManager mananger;
        mananger.InsertStatic({{"annotation1", "value1"}});
        mananger.InsertStatic({{"annotation1", "value2"}});
      },
      "Attempting to re-insert annotation1");
}

}  // namespace
}  // namespace forensics::feedback
