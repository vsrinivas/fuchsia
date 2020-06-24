// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_register.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/datastore.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ComponentData;
using testing::Pair;
using testing::UnorderedElementsAreArray;

class DataRegisterTest : public UnitTestFixture {
 public:
  DataRegisterTest() : datastore_(dispatcher(), services()), data_register_(&datastore_) {}

 protected:
  void Upsert(ComponentData data) {
    bool called_back = false;
    data_register_.Upsert(std::move(data), [&called_back]() { called_back = true; });
    RunLoopUntilIdle();
    FX_CHECK(called_back);
  }

  Datastore datastore_;
  DataRegister data_register_;
};

TEST_F(DataRegisterTest, Upsert_Basic) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));
}

TEST_F(DataRegisterTest, Upsert_DefaultNamespaceIfNoNamespaceProvided) {
  ComponentData data;
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("misc", UnorderedElementsAreArray({
                                   Pair("k", AnnotationOr("v")),
                               })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("misc.k", AnnotationOr("v")),
                                                      }));
}

TEST_F(DataRegisterTest, Upsert_NoInsertionsOnEmptyAnnotations) {
  ComponentData data;

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(), testing::IsEmpty());
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), testing::IsEmpty());
}

TEST_F(DataRegisterTest, Upsert_NoInsertionsOnReservedNamespace) {
  ComponentData data;
  data.set_namespace_(*kReservedAnnotationNamespaces.begin());
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(), testing::IsEmpty());
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), testing::IsEmpty());
}

TEST_F(DataRegisterTest, Upsert_NoUpdatesOnEmptyAnnotations) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));

  // We upsert another ComponentData with no annotations.
  ComponentData data2;

  Upsert(std::move(data2));

  // We check that the DataRegister's namespaced annotations and Datastore's non-platform
  // annotations are still the same.
  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));
}

TEST_F(DataRegisterTest, Upsert_InsertIfDifferentNamespaces) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));

  // We upsert another ComponentData with the same annotations, but under a different namespace.
  ComponentData data2;
  data2.set_namespace_("namespace2");
  data2.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
                  Pair("namespace2", UnorderedElementsAreArray({
                                         Pair("k", AnnotationOr("v")),
                                     })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                          Pair("namespace2.k", AnnotationOr("v")),
                                                      }));
}

TEST_F(DataRegisterTest, Upsert_InsertIfDifferentKey) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));

  // We upsert another ComponentData under the same namespace, but with a different key.
  ComponentData data2;
  data2.set_namespace_("namespace");
  data2.set_annotations({
      {"k2", "v2"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                        Pair("k2", AnnotationOr("v2")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                          Pair("namespace.k2", AnnotationOr("v2")),
                                                      }));
}

TEST_F(DataRegisterTest, Upsert_UpdateIfSameKey) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v")),
                                                      }));

  // We upsert another ComponentData under the same namespace and the same key.
  ComponentData data2;
  data2.set_namespace_("namespace");
  data2.set_annotations({
      {"k", "v2"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", AnnotationOr("v2")),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetNonPlatformAnnotations(), UnorderedElementsAreArray({
                                                          Pair("namespace.k", AnnotationOr("v2")),
                                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
