// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/syslog/logger.h>
#include <lib/syslog/structured_backend/fuchsia_syslog.h>

#include <gtest/gtest.h>
#include <sdk/lib/syslog/cpp/log_level.h>

template <typename T, size_t Count>
static constexpr bool MultiEquals(const T values[Count]) {
  for (size_t i = 1; i < Count; i++) {
    if (values[i] != values[i - 1]) {
      return false;
    }
  }
  return true;
}

TEST(HeaderTest, CompileTimeAsserts) {
  // NOTE: Please ensure that all 4 headers above are updated
  // BEFORE approving a change to this file. All 4 files must be
  // manually kept in-sync, and this test needs to be kept up-to-date
  // to prevent future inadvertent breakages. Reviewers MUST make
  // sure that anything new added to those header files
  // are properly tested in this file prior to approval.
  static_assert(FX_LOG_LEGACY_API_VERSION == 0);
  constexpr int versions[] = {FX_LOG_LEGACY_API_VERSION, FUCHSIA_LOG_API_VERSION,
                              FX_LOG_INTREE_API_VERSION};
  static_assert(MultiEquals<int, 3>(versions));
  constexpr int traces[] = {FX_LOG_TRACE, FUCHSIA_LOG_TRACE, syslog::LOG_TRACE,
                            static_cast<uint8_t>(fuchsia::diagnostics::Severity::TRACE)};
  static_assert(MultiEquals<int, 4>(traces));
  constexpr int debugs[] = {FX_LOG_DEBUG, FUCHSIA_LOG_DEBUG, syslog::LOG_DEBUG,
                            static_cast<uint8_t>(fuchsia::diagnostics::Severity::DEBUG)};
  static_assert(MultiEquals<int, 4>(debugs));
  constexpr int infos[] = {FX_LOG_INFO, FUCHSIA_LOG_INFO, syslog::LOG_INFO,
                           static_cast<uint8_t>(fuchsia::diagnostics::Severity::INFO)};
  static_assert(MultiEquals<int, 4>(infos));
  constexpr int errors[] = {FX_LOG_ERROR, FUCHSIA_LOG_ERROR, syslog::LOG_ERROR,
                            static_cast<uint8_t>(fuchsia::diagnostics::Severity::ERROR)};
  static_assert(MultiEquals<int, 4>(errors));
  constexpr int warns[] = {FX_LOG_WARNING, FUCHSIA_LOG_WARNING, syslog::LOG_WARNING,
                           static_cast<uint8_t>(fuchsia::diagnostics::Severity::WARN)};
  static_assert(MultiEquals<int, 4>(warns));
  constexpr int fatals[] = {FX_LOG_FATAL, FUCHSIA_LOG_FATAL, syslog::LOG_FATAL,
                            static_cast<uint8_t>(fuchsia::diagnostics::Severity::FATAL)};
  static_assert(MultiEquals<int, 4>(fatals));
  constexpr int nones[] = {FX_LOG_NONE, FUCHSIA_LOG_NONE, syslog::LOG_NONE};
  static_assert(MultiEquals<int, 3>(nones));
  constexpr int severity_steps[] = {FX_LOG_SEVERITY_STEP_SIZE, FUCHSIA_LOG_SEVERITY_STEP_SIZE,
                                    syslog::LogSeverityStepSize};
  static_assert(MultiEquals<int, 3>(severity_steps));
  constexpr int verbosity_steps[] = {FX_LOG_VERBOSITY_STEP_SIZE, FUCHSIA_LOG_VERBOSITY_STEP_SIZE,
                                     syslog::LogVerbosityStepSize};
  static_assert(MultiEquals<int, 3>(verbosity_steps));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
