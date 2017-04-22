// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>

mx_handle_t bootdata_get_bootfs(mx_handle_t log, mx_handle_t vmar_self,
                                mx_handle_t bootdata_vmo);

#pragma GCC visibility pop
