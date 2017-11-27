// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_IO_PORT_H_
#define GARNET_LIB_MACHINA_ARCH_X86_IO_PORT_H_

#include <fbl/mutex.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

class PicHandler : public IoHandler {
public:
    zx_status_t Init(Guest* guest, uint16_t base);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class PitHandler : public IoHandler {
public:
    zx_status_t Init(Guest* guest);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class Pm1Handler : public IoHandler {
public:
    zx_status_t Init(Guest* guest);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

private:
    mutable fbl::Mutex mutex_;
    uint16_t enable_ __TA_GUARDED(mutex_) = 0;
};

class RtcHandler : public IoHandler {
public:
    zx_status_t Init(Guest* guest);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

private:
    zx_status_t ReadRtcRegister(uint8_t rtc_index, uint8_t* value) const;
    zx_status_t WriteRtcRegister(uint8_t rtc_index, uint8_t value);
    mutable fbl::Mutex mutex_;
    uint8_t index_ __TA_GUARDED(mutex_) = 0;
};

class I8042Handler : public IoHandler {
public:
    zx_status_t Init(Guest* guest);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

private:
    mutable fbl::Mutex mutex_;
    uint8_t command_ __TA_GUARDED(mutex_) = 0;
};

class IoPort {
public:
    zx_status_t Init(Guest* guest);

private:
    PicHandler pic1_;
    PicHandler pic2_;
    PitHandler pit_;
    Pm1Handler pm1_;
    RtcHandler rtc_;
    I8042Handler i8042_;
};

#endif  // GARNET_LIB_MACHINA_ARCH_X86_IO_PORT_H_
