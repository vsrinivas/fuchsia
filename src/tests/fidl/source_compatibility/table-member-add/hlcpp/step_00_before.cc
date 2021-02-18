// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/tablememberadd/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::tablememberadd;

// [START contents]
void use_table(const fidl_test::Profile& profile) {
  if (profile.has_timezone()) {
    printf("timezone: %s", profile.timezone().c_str());
  }
  if (profile.has_temperature_unit()) {
    printf("preferred unit: %s", profile.temperature_unit().c_str());
  }
  for (const auto& entry : profile.UnknownData()) {
    printf("unknown ordinal %lu", entry.first);
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
