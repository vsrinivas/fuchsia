// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
#define ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_

#include <lib/boot-options/boot-options.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

void StandaloneInitIo(zx::unowned_resource root_resource);

[[gnu::weak]] zx::unowned_vmo StandaloneGetVmo(const std::string& name);

[[gnu::weak]] const BootOptions& StandaloneGetBootOptions();

struct StandaloneOption {
  std::string_view prefix;
  std::string option = {};
};

void StandaloneGetOptions(std::initializer_list<std::reference_wrapper<StandaloneOption>> opts);

#endif  // ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
