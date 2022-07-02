// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTING_STANDALONE_TEST_INCLUDE_LIB_STANDALONE_TEST_STANDALONE_H_
#define SRC_ZIRCON_TESTING_STANDALONE_TEST_INCLUDE_LIB_STANDALONE_TEST_STANDALONE_H_

#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

// Forward declaration for <lib/boot-options/boot-options.h>.
struct BootOptions;

namespace standalone {

struct Option {
  std::string_view prefix;
  std::string option = {};
};

void GetOptions(std::initializer_list<std::reference_wrapper<Option>> opts);

zx::unowned_resource GetRootResource();
zx::unowned_resource GetMmioRootResource();
zx::unowned_resource GetSystemRootResource();

zx::unowned_vmo GetVmo(const std::string& name);

const BootOptions& GetBootOptions();

// This is also wired up as write on STDOUT_FILENO or STDERR_FILENO.
// It does line-buffering and at '\n' boundaries it writes to the debuglog.
void LogWrite(std::string_view str);

}  // namespace standalone

#endif  // SRC_ZIRCON_TESTING_STANDALONE_TEST_INCLUDE_LIB_STANDALONE_TEST_STANDALONE_H_
