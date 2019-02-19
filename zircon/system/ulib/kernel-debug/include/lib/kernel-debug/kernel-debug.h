// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/svc/service.h>

const zx_service_provider_t* kernel_debug_get_service_provider(void);
