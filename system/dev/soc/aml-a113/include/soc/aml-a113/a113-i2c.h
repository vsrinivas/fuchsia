// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <soc/aml-a113/aml-i2c.h>

typedef struct {
    i2c_protocol_t proto;
    aml_i2c_dev_t* i2c_devs[AML_I2C_COUNT];
} a113_i2c_t;

zx_status_t a113_i2c_init(a113_i2c_t* i2c);
