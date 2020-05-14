// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-cr50.h"

#include <lib/device-protocol/i2c.h>
#include <lib/zx/time.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

namespace tpm {

constexpr zx::duration I2cCr50Interface::kNoIrqTimeout;
constexpr zx::duration I2cCr50Interface::kI2cRetryDelay;
constexpr size_t kNumI2cTries = 3;

I2cCr50Interface::I2cCr50Interface(zx_device_t* i2c_dev, zx::handle irq)
    : i2c_(i2c_dev), irq_(std::move(irq)) {}

I2cCr50Interface::~I2cCr50Interface() {}

zx_status_t I2cCr50Interface::Create(zx_device_t* i2c_dev, zx::handle irq,
                                     std::unique_ptr<I2cCr50Interface>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<I2cCr50Interface> iface(new (&ac) I2cCr50Interface(i2c_dev, std::move(irq)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  *out = std::move(iface);
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
    zxlogf(DEBUG, "tpm: Waiting for IRQ");
    zx_status_t status = zx_interrupt_wait(irq_.get(), nullptr);
    if (status != ZX_OK) {
      return status;
    }

    zxlogf(DEBUG, "tpm: Received IRQ");
  } else {
    zx::nanosleep(zx::deadline_after(kNoIrqTimeout));
  }
  return ZX_OK;
}

zx_status_t I2cCr50Interface::ReadAccess(Locality loc, uint8_t* access) {
  zxlogf(DEBUG, "tpm: Reading Access");
  zx_status_t status = RegisterRead(RegisterAccess(loc), access);
  zxlogf(DEBUG, "tpm: Read access: %08x %d", *access, status);
  return status;
}

zx_status_t I2cCr50Interface::WriteAccess(Locality loc, uint8_t access) {
  zxlogf(DEBUG, "tpm: Writing Access");
  return RegisterWrite(RegisterAccess(loc), access);
}

zx_status_t I2cCr50Interface::ReadStatus(Locality loc, uint32_t* sts) {
  zxlogf(DEBUG, "tpm: Reading Status");
  zx_status_t status = RegisterRead(RegisterStatus(loc), sts);
  zxlogf(DEBUG, "tpm: Read status: %08x %d", *sts, status);
  return status;
}

zx_status_t I2cCr50Interface::WriteStatus(Locality loc, uint32_t sts) {
  zxlogf(DEBUG, "tpm: Writing Status");
  return RegisterWrite(RegisterStatus(loc), sts);
}

zx_status_t I2cCr50Interface::ReadDidVid(uint16_t* vid, uint16_t* did) {
  zxlogf(DEBUG, "tpm: Reading DidVid");
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
  zxlogf(DEBUG, "tpm: Reading %zu bytes from DataFifo", len);
  return RegisterRead(RegisterDataFifo(loc), buf, len);
}

zx_status_t I2cCr50Interface::WriteDataFifo(Locality loc, const uint8_t* buf, size_t len) {
  zxlogf(DEBUG, "tpm: Writing %zu bytes to DataFifo", len);
  return RegisterWrite(RegisterDataFifo(loc), buf, len);
}

zx_status_t I2cCr50Interface::I2cReadLocked(uint8_t* val, size_t len) {
  if (len > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }

  i2c_protocol_t proto;
  i2c_.GetProto(&proto);

  zx_status_t status;
  for (size_t attempt = 0; attempt < kNumI2cTries; ++attempt) {
    if (attempt) {
      zxlogf(DEBUG, "i2c-tpm: Retrying read");
      zx::nanosleep(zx::deadline_after(kI2cRetryDelay));
    }

    status = i2c_read_sync(&proto, val, static_cast<uint32_t>(len));
    if (status == ZX_OK) {
      break;
    }
  }
  return status;
}

zx_status_t I2cCr50Interface::I2cWriteLocked(const uint8_t* val, size_t len) {
  if (len > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }

  i2c_protocol_t proto;
  i2c_.GetProto(&proto);

  zx_status_t status;
  for (size_t attempt = 0; attempt < kNumI2cTries; ++attempt) {
    if (attempt) {
      zxlogf(DEBUG, "i2c-tpm: Retrying write");
      zx::nanosleep(zx::deadline_after(kI2cRetryDelay));
    }

    status = i2c_write_sync(&proto, val, static_cast<uint32_t>(len));
    if (status == ZX_OK) {
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
    zxlogf(ERROR, "i2c-tpm: writing address failed");
    return status;
  }

  status = WaitForIrqLocked();
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-tpm: waiting for IRQ failed");
    return status;
  }

  status = I2cReadLocked(out, len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-tpm: read from %#x failed", reg.addr);
    return status;
  }

  return ZX_OK;
}

zx_status_t I2cCr50Interface::RegisterWrite(const I2cRegister<uint8_t[]>& reg, const uint8_t* val,
                                            size_t len) {
  fbl::AutoLock guard(&lock_);

  // TODO(teisenbe): Don't allocate here
  size_t msg_len = len + 1;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[msg_len]);
  buf[0] = reg.addr;
  memcpy(buf.get() + 1, val, len);

  zx_status_t status = I2cWriteLocked(buf.get(), msg_len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-tpm: write to %#x failed", reg.addr);
    return status;
  }

  // Wait for IRQ indicating write received
  status = WaitForIrqLocked();
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-tpm: waiting for IRQ failed");
    return status;
  }

  return ZX_OK;
}

}  // namespace tpm
