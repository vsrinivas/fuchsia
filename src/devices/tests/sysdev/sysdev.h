// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_SYSDEV_SYSDEV_H_
#define SRC_DEVICES_TESTS_SYSDEV_SYSDEV_H_

#include <lib/ddk/device.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

zx_status_t test_sysdev_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                               zx_handle_t rpc_channel);

__END_CDECLS

#endif  // SRC_DEVICES_TESTS_SYSDEV_SYSDEV_H_
