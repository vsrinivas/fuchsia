// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/mutex.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <stdint.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "tpm.h"

namespace tpm {

class I2cCr50Interface : public HardwareInterface {
public:
    // Creates a new I2cCr50Interface from the given |i2c_dev|.  This will issue an
    // I2C transaction to determine support.
    static zx_status_t Create(zx_device_t* i2c_dev, zx::handle irq,
                              fbl::unique_ptr<I2cCr50Interface>* out);
    virtual ~I2cCr50Interface();

    zx_status_t Validate() override;

    zx_status_t ReadAccess(Locality loc, uint8_t* access) override;
    zx_status_t WriteAccess(Locality loc, uint8_t access) override;

    zx_status_t ReadStatus(Locality loc, uint32_t* sts) override;
    zx_status_t WriteStatus(Locality loc, uint32_t sts) override;

    zx_status_t ReadDidVid(uint16_t* vid, uint16_t* did) override;

    zx_status_t ReadDataFifo(Locality loc, uint8_t* buf, size_t len) override;
    zx_status_t WriteDataFifo(Locality loc, const uint8_t* buf, size_t len) override;

private:
    template <typename T> struct I2cRegister {
        explicit constexpr I2cRegister(uint8_t addr) : addr(addr) { }
        const uint8_t addr;
    };

    // Timeout to use if this device does not have an IRQ wired up.
    static constexpr zx::duration kNoIrqTimeout = zx::msec(20);
    // Delay to use between retries if an I2C operation errors.
    static constexpr zx::duration kI2cRetryDelay = zx::usec(50);

    I2cCr50Interface(zx_device_t* i2c_dev, zx::handle irq);

    // Block until the controller signals it is ready.  May return spuriously,
    // so the condition being waited on should be checked after return.
    zx_status_t WaitForIrqLocked() TA_REQ(lock_);

    // Template for enforcing correct access size for each register read
    template <typename T>
    zx_status_t RegisterRead(const I2cRegister<T>& reg, T* out) {
        // TODO(teisenbe): If we ever support a big-endian host, we need to do
        // endianness swapping here.
        static_assert(fbl::is_integral<T>::value, "T must be integral");
        return RegisterRead(I2cRegister<uint8_t[]>(reg.addr), reinterpret_cast<uint8_t*>(out),
                            sizeof(T));
    }

    // Template for enforcing correct access size for each register write
    template <typename T>
    zx_status_t RegisterWrite(const I2cRegister<T>& reg, const T& val) {
        static_assert(fbl::is_integral<T>::value, "T must be integral");
        return RegisterWrite(I2cRegister<uint8_t[]>(reg.addr),
                             reinterpret_cast<const uint8_t*>(&val), sizeof(T));
    }

    // Perform an I2C read cycle
    zx_status_t I2cReadLocked(uint8_t* val, size_t len) TA_REQ(lock_);
    // Perform an I2C write cycle
    zx_status_t I2cWriteLocked(const uint8_t* val, size_t len) TA_REQ(lock_);

    // Perform a register read/write for an unsized register (indicated
    // by T=uint8_t[]).
    zx_status_t RegisterWrite(const I2cRegister<uint8_t[]>& reg,
                              const uint8_t* val, size_t len) TA_EXCL(lock_);
    zx_status_t RegisterRead(const I2cRegister<uint8_t[]>& reg,
                             uint8_t* out, size_t len) TA_EXCL(lock_);

    // Compute the register address prefix for the given locality
    static constexpr uint8_t LocToPrefix(Locality loc) {
        ZX_DEBUG_ASSERT(loc <= 4);
        return static_cast<uint8_t>(loc << 4);
    }

    // These methods return an object usable with RegisterRead/RegisterWrite representing
    // the specified register and locality.
    static constexpr I2cRegister<uint8_t> RegisterAccess(Locality loc) {
        return I2cRegister<uint8_t>(static_cast<uint8_t>(LocToPrefix(loc) | 0x0u));
    }
    static constexpr I2cRegister<uint32_t> RegisterStatus(Locality loc) {
        ZX_DEBUG_ASSERT(loc <= 4);
        return I2cRegister<uint32_t>(static_cast<uint8_t>(LocToPrefix(loc) | 0x1u));
    }
    static constexpr I2cRegister<uint8_t[]> RegisterDataFifo(Locality loc) {
        ZX_DEBUG_ASSERT(loc <= 4);
        return I2cRegister<uint8_t[]>(static_cast<uint8_t>(LocToPrefix(loc) | 0x5u));
    }
    static constexpr I2cRegister<uint32_t> RegisterDidVid(Locality loc) {
        ZX_DEBUG_ASSERT(loc <= 4);
        return I2cRegister<uint32_t>(static_cast<uint8_t>(LocToPrefix(loc) | 0x6u));
    }

    fbl::Mutex lock_;

    // The upstream i2c device
    zx_device_t* i2c_ TA_GUARDED(lock_) = nullptr;
    zx::handle irq_ TA_GUARDED(lock_);
};

} // namespace tpm
