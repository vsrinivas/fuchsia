// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_RESOURCE_INCLUDE_LIB_FAKE_RESOURCE_RESOURCE_H_
#define SRC_DEVICES_TESTING_FAKE_RESOURCE_INCLUDE_LIB_FAKE_RESOURCE_RESOURCE_H_

#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t fake_root_resource_create(zx_handle_t *out);

__END_CDECLS

#endif  // SRC_DEVICES_TESTING_FAKE_RESOURCE_INCLUDE_LIB_FAKE_RESOURCE_RESOURCE_H_
