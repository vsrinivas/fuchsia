// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
#define ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

void StandaloneInitIo(zx_handle_t root_resource);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_UTEST_CORE_STANDALONE_H_
