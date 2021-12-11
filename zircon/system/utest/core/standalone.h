// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
#define ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_

#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

void StandaloneInitIo(zx::unowned_resource root_resource);

#endif  // ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
