// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/data_register.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

using fuchsia::feedback::ComponentData;
using testing::Pair;
using testing::UnorderedElementsAreArray;

constexpr char kReservedNamespace[] = "reserved-namespace";

class DataRegisterTest : public UnitTestFixture {
 public:
  DataRegisterTest() { MakeNewDataRegister(); }

 protected:
  void Upsert(ComponentData data) {
    bool called_back = false;
    data_register_->Upsert(std::move(data), [&called_back]() { called_back = true; });
    RunLoopUntilIdle();
    FX_CHECK(called_back);
  }

  std::string RegisterJsonPath() { return files::JoinPath(tmp_dir_.path(), "register.json"); }

  std::string ReadRegisterJson() {
    std::string json;
    files::ReadFileToString(RegisterJsonPath(), &json);
    return json;
  }

  void MakeNewDataRegister(const size_t max_size = std::numeric_limits<size_t>::max()) {
    data_register_ = std::make_unique<DataRegister>(
        max_size, std::set<std::string>({kReservedNamespace}), RegisterJsonPath());
  }

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<DataRegister> data_register_;
};

TEST_F(DataRegisterTest, Upsert_Basic) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_DefaultNamespaceIfNoNamespaceProvided) {
  ComponentData data;
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("misc.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "misc": {
        "k": "v"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_NoInsertionsOnEmptyAnnotations) {
  ComponentData data;

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), testing::IsEmpty());
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_TRUE(ReadRegisterJson().empty());
}

TEST_F(DataRegisterTest, Upsert_NoInsertionsOnReservedNamespace) {
  ComponentData data;
  data.set_namespace_(kReservedNamespace);
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), testing::IsEmpty());
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_TRUE(ReadRegisterJson().empty());
}

TEST_F(DataRegisterTest, Upsert_NoInsertionsOnTooMany) {
  MakeNewDataRegister(/*max_size=*/1);
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");

  ComponentData data2;
  data2.set_namespace_("namespace");
  data2.set_annotations({
      {"k2", "v2"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_TRUE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_NoUpdatesOnEmptyAnnotations) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");

  // We upsert another ComponentData with no annotations.
  ComponentData data2;

  Upsert(std::move(data2));

  // We check that the DataRegister's namespaced annotations and Datastore's non-platform
  // annotations are still the same.
  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_InsertIfDifferentNamespaces) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");

  // We upsert another ComponentData with the same annotations, but under a different namespace.
  ComponentData data2;
  data2.set_namespace_("namespace2");
  data2.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                         Pair("namespace2.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    },
    "namespace2": {
        "k": "v"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_InsertIfDifferentKey) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());

  // We upsert another ComponentData under the same namespace, but with a different key.
  ComponentData data2;
  data2.set_namespace_("namespace");
  data2.set_annotations({
      {"k2", "v2"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                         Pair("namespace.k2", "v2"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v",
        "k2": "v2"
    }
})");
}

TEST_F(DataRegisterTest, Upsert_UpdateIfSameKey) {
  ComponentData data;
  data.set_namespace_("namespace");
  data.set_annotations({
      {"k", "v"},
  });

  Upsert(std::move(data));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v"
    }
})");

  // We upsert another ComponentData under the same namespace and the same key.
  ComponentData data2;
  data2.set_namespace_("namespace");
  data2.set_annotations({
      {"k", "v2"},
  });

  Upsert(std::move(data2));

  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace.k", "v2"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
  EXPECT_EQ(ReadRegisterJson(), R"({
    "namespace": {
        "k": "v2"
    }
})");
}

TEST_F(DataRegisterTest, ReinitializesFromJson) {
  ComponentData data1;
  data1.set_namespace_("namespace1");
  data1.set_annotations({
      {"k1", "v1"},
      {"k2", "v2"},
  });

  Upsert(std::move(data1));

  ComponentData data2;
  data2.set_namespace_("namespace2");
  data2.set_annotations({
      {"k3", "v3"},
      {"k4", "v4"},
  });

  Upsert(std::move(data2));

  MakeNewDataRegister();
  EXPECT_THAT(data_register_->Get(), UnorderedElementsAreArray({
                                         Pair("namespace1.k1", "v1"),
                                         Pair("namespace1.k2", "v2"),
                                         Pair("namespace2.k3", "v3"),
                                         Pair("namespace2.k4", "v4"),
                                     }));
  EXPECT_FALSE(data_register_->IsMissingAnnotations());
}

}  // namespace
}  // namespace forensics::feedback
