// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#ifndef TEST
#include "bind.h"  //nogncheck
#else
#include "test_bind.h"  //nogncheck
#endif

extern struct zx_driver_ops msd_driver_ops;

ZIRCON_DRIVER(magma_pdev_gpu, msd_driver_ops, "zircon", "0.1");
