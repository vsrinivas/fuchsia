// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_util.h"

#include <map>
#include <string>

#include <gtest/gtest.h>

namespace forensics {
namespace crash_reports {
namespace {

TEST(Shorten, ShortensCorrectly) {
  const std::map<std::string, std::string> name_to_shortened_name = {
      // Does nothing.
      {"system", "system"},
      // Remove leading whitespace.
      {"    system", "system"},
      // Remove trailing whitespace.
      {"system    ", "system"},
      // Remove "fuchsia-pkg://" prefix.
      {"fuchsia-pkg://fuchsia.com/foo-bar#meta/foo_bar.cmx",
       "fuchsia.com:foo-bar#meta:foo_bar.cmx"},
      // Remove leading whitespace and "fuchsia-pkg://" prefix.
      {"     fuchsia-pkg://fuchsia.com/foo-bar#meta/foo_bar.cmx",
       "fuchsia.com:foo-bar#meta:foo_bar.cmx"},
      // Replaces runs of '/' with a single ':'.
      {"//////////test/", ":test:"},
  };

  for (const auto& [name, shortend_name] : name_to_shortened_name) {
    EXPECT_EQ(Shorten(name), shortend_name);
  }
}

TEST(Logname, MakesLognameCorrectly) {
  const std::map<std::string, std::string> name_to_logname = {
      // Does nothing.
      {"system", "system"},
      // Remove leading whitespace.
      {"    system", "system"},
      // Remove trailing whitespace.
      {"system    ", "system"},
      // Extracts components_for_foo
      {"bin/components_for_foo", "components_for_foo"},
      // Extracts foo_bar from the v1 URL.
      {"fuchsia-pkg://fuchsia.com/foo-bar#meta/foo_bar.cmx", "foo_bar"},
      // Extracts foo_bar from the v1 URL.
      {"fuchsia.com:foo-bar#meta:foo_bar.cmx", "foo_bar"},
      // Extracts foo_bar from the v2 URL.
      {"fuchsia-pkg://fuchsia.com/foo-bar#meta/foo_bar.cm", "foo_bar"},
      // Extracts foo_bar from the v2 URL.
      {"fuchsia.com:foo-bar#meta:foo_bar.cm", "foo_bar"},
  };

  for (const auto& [name, logname] : name_to_logname) {
    EXPECT_EQ(Logname(name), logname);
  }
}

TEST(MakeReport, AddsSnapshotAnnotations) {
  auto annotations = std::make_shared<AnnotationMap>(AnnotationMap({
      {"snapshot_annotation_key", "snapshot_annotation_value"},
  }));

  fuchsia::feedback::CrashReport crash_report;
  crash_report.set_program_name("program_name");

  Product product{
      .name = "product_name",
      .version = "product_version",
      .channel = "product_channel",
  };

  const auto report =
      MakeReport(std::move(crash_report), /*report_id=*/0, "snapshot_uuid", Snapshot(annotations),
                 /*current_time=*/std::nullopt, ::fpromise::ok("device_id"),
                 AnnotationMap({{"key", "value"}}), product, /*is_hourly_report=*/false);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report.value().Annotations().Get("snapshot_annotation_key"),
            "snapshot_annotation_value");
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
