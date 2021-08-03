// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// When MVM isn't otherwise linked into a test, we will need to provide implementations for some of
// its entry points that are not called but still linked in.

#include <zircon/errors.h>
#include <zircon/types.h>

#if defined(CPTCFG_IWLMVM)

// For platform/module.cc.
extern "C" zx_status_t iwl_mvm_init() { return ZX_ERR_NOT_SUPPORTED; }

#endif  // defined(CPTCFG_IWLMVM)
