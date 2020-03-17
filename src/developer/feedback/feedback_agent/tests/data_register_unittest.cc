// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_register.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include "src/developer/feedback/feedback_agent/datastore.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
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
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
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
                                   Pair("k", "v"),
                               })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("misc.k", "v"),
                                                }));
}

TEST_F(DataRegisterTest, Upsert_EmptyAnnotationsOnNewEmptyAnnotations) {
  ComponentData data;

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(), testing::IsEmpty());
  EXPECT_THAT(datastore_.GetExtraAnnotations(), testing::IsEmpty());
}

TEST_F(DataRegisterTest, Upsert_AnnotationsNotClearedOnNewEmptyAnnotations) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
                                                }));

  // We upsert another ComponentData with no annotations.
  ComponentData data2;

  Upsert(std::move(data2));

  // We check that the DataRegister's namespaced annotations and Datastore's extra annotations are
  // still the same.
  EXPECT_THAT(data_register_.GetNamespacedAnnotations(),
              UnorderedElementsAreArray({
                  Pair("namespace", UnorderedElementsAreArray({
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
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
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
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
                                        Pair("k", "v"),
                                    })),
                  Pair("namespace2", UnorderedElementsAreArray({
                                         Pair("k", "v"),
                                     })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
                                                    Pair("namespace2.k", "v"),
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
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
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
                                        Pair("k", "v"),
                                        Pair("k2", "v2"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
                                                    Pair("namespace.k2", "v2"),
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
                                        Pair("k", "v"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v"),
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
                                        Pair("k", "v2"),
                                    })),
              }));
  EXPECT_THAT(datastore_.GetExtraAnnotations(), UnorderedElementsAreArray({
                                                    Pair("namespace.k", "v2"),
                                                }));
}

}  // namespace
}  // namespace feedback
