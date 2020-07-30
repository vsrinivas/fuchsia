// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_MINIDUMP_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_MINIDUMP_H_

#include <lib/fit/promise.h>
#include <lib/zx/exception.h>
#include <lib/zx/vmo.h>

#include <third_party/crashpad/util/file/string_file.h>

namespace forensics {
namespace exceptions {

// If |string_file| is empty, this function will error out.
// The resulting vmo will not be valid on error.
// Mostly exposed for testing purposes, but valid as a standalone function.
zx::vmo GenerateVMOFromStringFile(const crashpad::StringFile& string_file);

::fit::promise<zx::vmo> GenerateMinidumpVMO(zx::exception exception);

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_MINIDUMP_H_
