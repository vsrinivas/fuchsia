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
      // Extracts foo_bar.
      {"fuchsia-pkg://fuchsia.com/foo-bar#meta/foo_bar.cmx", "foo_bar"},
      // Extracts foo_bar.
      {"fuchsia.com:foo-bar#meta:foo_bar.cmx", "foo_bar"},
  };

  for (const auto& [name, logname] : name_to_logname) {
    EXPECT_EQ(Logname(name), logname);
  }
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
