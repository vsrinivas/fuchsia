// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/project_profile.h"

#include "gtest/gtest.h"
#include "src/lib/cobalt/cpp/test_metrics_registry.cb.h"

namespace cobalt {

TEST(ProjectProfile, FromString) { ProjectProfileFromString(""); }

TEST(ProjectProfile, FromBase64String) {
  ProjectProfileFromBase64String(cobalt_test_metrics::kConfig);
}

TEST(ProjectProfile, FromFile) {
  ProjectProfileFromFile("/pkg/data/test_metrics_registry.pb");
}

}  // namespace cobalt
