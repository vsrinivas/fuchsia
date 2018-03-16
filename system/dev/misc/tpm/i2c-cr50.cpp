// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-cr50.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <string.h>
#include <lib/zx/time.h>

namespace tpm {

constexpr zx::duration I2cCr50Interface::kNoIrqTimeout;
constexpr zx::duration I2cCr50Interface::kI2cRetryDelay;
constexpr size_t kNumI2cTries = 3;

I2cCr50Interface::I2cCr50Interface(zx_device_t* i2c_dev, zx::handle irq)
        : i2c_(i2c_dev), irq_(fbl::move(irq)) {
}

I2cCr50Interface::~I2cCr50Interface() {
}

zx_status_t I2cCr50Interface::Create(zx_device_t* i2c_dev, zx::handle irq,
                                     fbl::unique_ptr<I2cCr50Interface>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<I2cCr50Interface> iface(new (&ac) I2cCr50Interface(i2c_dev, fbl::move(irq)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    *out = fbl::move(iface);
    return ZX_OK;
}

zx_status_t I2cCr50Interface::Validate() {
    uint16_t vid, did;
    zx_status_t status = ReadDidVid(&did, &vid);
    if (status != ZX_OK) {
        return status;
    }
    if (vid != 0x1ae0 || did != 0x0028) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t I2cCr50Interface::WaitForIrqLocked() {
    if (irq_) {
        zxlogf(TRACE, "tpm: Waiting for IRQ\n");
        uint64_t slots;
        zx_status_t status = zx_interrupt_wait(irq_.get(), &slots);
        if (status != ZX_OK) {
            return status;
        }

        zxlogf(TRACE, "tpm: Received IRQ\n");
    } else {
        zx::nanosleep(zx::deadline_after(kNoIrqTimeout));
    }
    return ZX_OK;
}

zx_status_t I2cCr50Interface::ReadAccess(Locality loc, uint8_t* access) {
    zxlogf(TRACE, "tpm: Reading Access\n");
    zx_status_t status = RegisterRead(RegisterAccess(loc), access);
    zxlogf(TRACE, "tpm: Read access: %08x %d\n", *access, status);
    return status;
}

zx_status_t I2cCr50Interface::WriteAccess(Locality loc, uint8_t access) {
    zxlogf(TRACE, "tpm: Writing Access\n");
    return RegisterWrite(RegisterAccess(loc), access);
}

zx_status_t I2cCr50Interface::ReadStatus(Locality loc, uint32_t* sts) {
    zxlogf(TRACE, "tpm: Reading Status\n");
    zx_status_t status = RegisterRead(RegisterStatus(loc), sts);
    zxlogf(TRACE, "tpm: Read status: %08x %d\n", *sts, status);
    return status;
}

zx_status_t I2cCr50Interface::WriteStatus(Locality loc, uint32_t sts) {
    zxlogf(TRACE, "tpm: Writing Status\n");
    return RegisterWrite(RegisterStatus(loc), sts);
}

zx_status_t I2cCr50Interface::ReadDidVid(uint16_t* vid, uint16_t* did) {
    zxlogf(TRACE, "tpm: Reading DidVid\n");
    uint32_t value;
    zx_status_t status = RegisterRead(RegisterDidVid(0), &value);
    if (status != ZX_OK) {
        return status;
    }
    *vid = static_cast<uint16_t>(value >> 16);
    *did = static_cast<uint16_t>(value);
    return ZX_OK;
}

zx_status_t I2cCr50Interface::ReadDataFifo(Locality loc, uint8_t* buf, size_t len) {
    zxlogf(TRACE, "tpm: Reading %zu bytes from DataFifo\n", len);
    return RegisterRead(RegisterDataFifo(loc), buf, len);
}

zx_status_t I2cCr50Interface::WriteDataFifo(Locality loc, const uint8_t* buf, size_t len) {
    zxlogf(TRACE, "tpm: Writing %zu bytes to DataFifo\n", len);
    return RegisterWrite(RegisterDataFifo(loc), buf, len);
}

zx_status_t I2cCr50Interface::I2cReadLocked(uint8_t* val, size_t len) {
    zx_status_t status;
    for (size_t attempt = 0; attempt < kNumI2cTries; ++attempt) {
        if (attempt) {
            zxlogf(TRACE, "i2c-tpm: Retrying read\n");
            zx::nanosleep(zx::deadline_after(kI2cRetryDelay));
        }

        size_t actual;
        status = device_read(i2c_, val, len, 0, &actual);
        if (status == ZX_OK) {
            if (actual != len) {
                zxlogf(ERROR, "i2c-tpm: short read: %zu vs %zu\n", actual, len);
                return ZX_ERR_IO;
            }
            break;
        }
    }
    return status;
}

zx_status_t I2cCr50Interface::I2cWriteLocked(const uint8_t* val, size_t len) {
    zx_status_t status;
    for (size_t attempt = 0; attempt < kNumI2cTries; ++attempt) {
        if (attempt) {
            zxlogf(TRACE, "i2c-tpm: Retrying write\n");
            zx::nanosleep(zx::deadline_after(kI2cRetryDelay));
        }

        size_t actual;
        status = device_write(i2c_, val, len, 0, &actual);
        if (status == ZX_OK) {
            if (actual != len) {
                zxlogf(ERROR, "i2c-tpm: short write: %zu vs %zu\n", actual, len);
                return ZX_ERR_IO;
            }
            break;
        }
    }
    return status;
}

zx_status_t I2cCr50Interface::RegisterRead(const I2cRegister<uint8_t[]>& reg, uint8_t* out,
                                           size_t len) {
    fbl::AutoLock guard(&lock_);

    // TODO(teisenbe): Using a repeated start would be preferred here for
    // throughput, but I2C TPM devices are not required to support it.  We
    // can test for support and use it if possible.

    zx_status_t status = I2cWriteLocked(&reg.addr, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c-tpm: writing address failed\n");
        return status;
    }

    status = WaitForIrqLocked();
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c-tpm: waiting for IRQ failed\n");
        return status;
    }

    status = I2cReadLocked(out, len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c-tpm: read from %#x failed\n", reg.addr);
        return status;
    }

    return ZX_OK;
}

zx_status_t I2cCr50Interface::RegisterWrite(const I2cRegister<uint8_t[]>& reg, const uint8_t* val,
                                            size_t len) {
    fbl::AutoLock guard(&lock_);

    // TODO(teisenbe): Don't allocate here
    size_t msg_len = len + 1;
    fbl::unique_ptr<uint8_t[]> buf(new uint8_t[msg_len]);
    buf[0] = reg.addr;
    memcpy(buf.get() + 1, val, len);

    zx_status_t status = I2cWriteLocked(buf.get(), msg_len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c-tpm: write to %#x failed\n", reg.addr);
        return status;
    }

    // Wait for IRQ indicating write received
    status = WaitForIrqLocked();
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c-tpm: waiting for IRQ failed\n");
        return status;
    }

    return ZX_OK;
}

} // namespace tpm
