// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_I2C_H_
#define DDK_METADATA_I2C_H_

typedef struct {
    uint32_t bus_id;
    uint16_t address;
    // Used for binding directly to the I2C device using platform device IDs.
    // Set to zero if unused.
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
} i2c_channel_t;

#endif  // DDK_METADATA_I2C_H_
