// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddk/protocol/i2c-lib.h>
#include <fbl/macros.h>
#include <zircon/assert.h>

namespace ddk {

class I2cChannel {

public:
    friend class Pdev;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(I2cChannel);
    DISALLOW_NEW;

    I2cChannel() = default;

    I2cChannel(I2cChannel&& other) {
        i2c_.ops = other.i2c_.ops;
        i2c_.ctx = other.i2c_.ctx;
        pdev_index_ = other.pdev_index_;
        other.reset();
    }

    ~I2cChannel() = default;

    // Allow assignment from an rvalue
    I2cChannel& operator=(I2cChannel&& other) {
        i2c_.ops = other.i2c_.ops;
        i2c_.ctx = other.i2c_.ctx;
        pdev_index_ = other.pdev_index_;
        other.reset();
        return *this;
    }

    void reset() {
        i2c_ = {0, 0};
    }

    // Check to determine if this object is initialized
    bool is_valid() const {
        return (i2c_.ops && i2c_.ctx);
    }

    /*
        The following methods assume the I2cChannel has been successfully
        constructed/initialized by the friend class Pdev.  A crash will result
        from calling these methods on an uninitialized I2cChannel instance.

        is_valid() can be called at any time to safely check if the instance
        is properly initialized.
    */

    // Performs typical i2c Read: writes device register address (1 byte) followed
    //  by len reads into buf.
    zx_status_t Read(uint8_t addr, uint8_t* buf, uint8_t len) const {
        return Transact(&addr, 1, buf, len);
    }

    // Writes len bytes from buffer with no trailing read
    zx_status_t Write(uint8_t* buf, uint8_t len) const {
        return Transact(buf, len, nullptr, 0);
    }

    zx_status_t Transact(uint8_t* tx_buf, uint8_t tx_len,
                         uint8_t* rx_buf, uint8_t rx_len) const {
        return i2c_write_read_sync(&i2c_, tx_buf, tx_len, rx_buf, rx_len);
    }

private:
    //Constructor used by the friend class Pdev to create an initialized
    //  instance of I2cChannel
    I2cChannel(uint32_t index, i2c_protocol_t i2c)
        : pdev_index_(index),
          i2c_(i2c) {
    }

    uint32_t pdev_index_;
    i2c_protocol_t i2c_ = {0, 0};
};

} //namespace ddk
