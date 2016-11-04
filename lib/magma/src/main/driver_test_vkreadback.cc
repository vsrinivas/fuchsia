// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include "magma_util/dlog.h"
#include <ddk/device.h>

extern "C" {
#include "vkreadback/vkreadback.h"
}

void magma_indriver_test(mx_device_t* device)
{
    DLOG("running vkreadback tests\n");
    test_vk_readback();
}
