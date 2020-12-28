// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MINIDUMP_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MINIDUMP_H_

#include <lib/zx/exception.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <string>

#include <third_party/crashpad/util/file/string_file.h>

namespace forensics {
namespace exceptions {
namespace handler {

// Policy error exceptions that should be used to better form crash reports.
enum class PolicyError {
  kChannelOverflow,
  kPortOverflow,
};

// If |string_file| is empty, this function will error out.
// The resulting vmo will not be valid on error.
// Mostly exposed for testing purposes, but valid as a standalone function.
zx::vmo GenerateVMOFromStringFile(const crashpad::StringFile& string_file);

zx::vmo GenerateMinidump(const zx::exception& exception, std::optional<PolicyError>* policy_error);

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MINIDUMP_H_
