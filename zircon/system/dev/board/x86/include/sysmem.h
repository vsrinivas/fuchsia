// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/platform/bus.h>
#include <zircon/compiler.h>

zx_status_t publish_sysmem(pbus_protocol_t* pbus);
