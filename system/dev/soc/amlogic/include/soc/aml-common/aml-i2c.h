// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>

typedef enum {
    AML_I2C_A,
    AML_I2C_B,
    AML_I2C_C,
    AML_I2C_D,
    AML_I2C_COUNT,
} aml_i2c_port_t;
