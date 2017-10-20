// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/io_port.h>

#include <time.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>

#include "acpi_priv.h"

// clang-format off

/* PIC constatants. */
constexpr uint16_t kPicDataPort                 = 1;
constexpr uint8_t kPicInvalid                   = UINT8_MAX;

/* PM1 relative port mappings. */
constexpr uint16_t kPm1StatusPort               = PM1A_REGISTER_STATUS;
constexpr uint16_t kPm1EnablePort               = PM1A_REGISTER_ENABLE;
constexpr uint16_t kPm1ControlPort              = PM1_CONTROL_PORT - PM1_EVENT_PORT;
constexpr uint16_t kPm1Size                     = kPm1EnablePort + 1;

/* RTC relative port mappings. */
constexpr uint16_t kRtcIndexPort                = 0;
constexpr uint16_t kRtcDataPort                 = 1;

/* RTC register addresses. */
constexpr uint8_t kRtcRegisterSeconds           = 0;
constexpr uint8_t kRtcRegisterMinutes           = 2;
constexpr uint8_t kRtcRegisterHours             = 4;
constexpr uint8_t kRtcRegisterDayOfMonth        = 7;
constexpr uint8_t kRtcRegisterMonth             = 8;
constexpr uint8_t kRtcRegisterYear              = 9;
constexpr uint8_t kRtcRegisterA                 = 10;
constexpr uint8_t kRtcRegisterB                 = 11;

/* RTC register B flags. */
constexpr uint8_t kRtcRegisterBDaylightSavings  = 1 << 0;
constexpr uint8_t kRtcRegisterBHourFormat       = 1 << 1;

/* RTC relative port mappings. */
constexpr uint16_t kI8042DataPort               = 0x0;
constexpr uint16_t kI8042CommandPort            = 0x4;

/* I8042 status flags. */
constexpr uint8_t kI8042StatusOutputFull        = 1 << 0;

/* I8042 test constants. */
constexpr uint8_t kI8042CommandTest             = 0xaa;
constexpr uint8_t kI8042DataTestResponse        = 0x55;

// clang-format on

zx_status_t PicHandler::Init(Guest* guest, uint16_t base) {
    return guest->CreateMapping(TrapType::PIO_SYNC, base, PIC_SIZE, 0, this);
}

zx_status_t PicHandler::Read(uint64_t addr, IoValue* value) {
    if (addr == kPicDataPort) {
        value->access_size = 1;
        value->u8 = kPicInvalid;
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PicHandler::Write(uint64_t addr, const IoValue& value) {
    return ZX_OK;
}

zx_status_t PitHandler::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::PIO_SYNC, PIT_BASE, PIT_SIZE, 0, this);
}

zx_status_t PitHandler::Read(uint64_t addr, IoValue* value) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PitHandler::Write(uint64_t addr, const IoValue& value) {
    return ZX_OK;
}

zx_status_t Pm1Handler::Init(Guest* guest) {
    // Map 2 distinct register blocks for event and control registers.
    zx_status_t status = guest->CreateMapping(TrapType::PIO_SYNC, PM1_EVENT_PORT, kPm1Size, 0,
                                              this);
    if (status != ZX_OK)
        return status;
    return guest->CreateMapping(TrapType::PIO_SYNC, PM1_CONTROL_PORT, kPm1Size, kPm1ControlPort,
                                this);
}

zx_status_t Pm1Handler::Read(uint64_t addr, IoValue* value) {
    switch (addr) {
    case kPm1StatusPort:
        value->access_size = 2;
        value->u16 = 0;
        break;
    case kPm1EnablePort: {
        value->access_size = 2;
        fbl::AutoLock lock(&mutex_);
        value->u16 = enable_;
        break;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t Pm1Handler::Write(uint64_t addr, const IoValue& value) {
    switch (addr) {
    case kPm1EnablePort: {
        if (value.access_size != 2)
            return ZX_ERR_IO_DATA_INTEGRITY;
        fbl::AutoLock lock(&mutex_);
        enable_ = value.u16;
        break;
    }
    case kPm1ControlPort: {
        uint16_t slp_en = bit_shift(value.u16, 13);
        uint16_t slp_type = bits_shift(value.u16, 12, 10);
        if (slp_en != 0) {
            // Only power-off transitions are supported.
            return slp_type == SLP_TYP5 ? ZX_ERR_STOP : ZX_ERR_NOT_SUPPORTED;
        }
        break;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

static uint8_t to_bcd(int binary) {
    return static_cast<uint8_t>(((binary / 10) << 4) | (binary % 10));
}

zx_status_t RtcHandler::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::PIO_SYNC, RTC_BASE, RTC_SIZE, 0, this);
}

zx_status_t RtcHandler::Read(uint64_t port, IoValue* value) {
    if (port == kRtcDataPort) {
        value->access_size = 1;
        uint8_t rtc_index;
        {
            fbl::AutoLock lock(&mutex_);
            rtc_index = index_;
        }
        return HandleRtc(rtc_index, &value->u8);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t RtcHandler::Write(uint64_t addr, const IoValue& value) {
    if (addr == kRtcIndexPort) {
        if (value.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        fbl::AutoLock lock(&mutex_);
        index_ = value.u8;
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t RtcHandler::HandleRtc(uint8_t rtc_index, uint8_t* value) {
    time_t now = time(nullptr);
    struct tm tm;
    if (localtime_r(&now, &tm) == nullptr)
        return ZX_ERR_INTERNAL;
    switch (rtc_index) {
    case kRtcRegisterSeconds:
        *value = to_bcd(tm.tm_sec);
        break;
    case kRtcRegisterMinutes:
        *value = to_bcd(tm.tm_min);
        break;
    case kRtcRegisterHours:
        *value = to_bcd(tm.tm_hour);
        break;
    case kRtcRegisterDayOfMonth:
        *value = to_bcd(tm.tm_mday);
        break;
    case kRtcRegisterMonth:
        *value = to_bcd(tm.tm_mon);
        break;
    case kRtcRegisterYear: {
        // RTC expects the number of years since 2000.
        int year = tm.tm_year - 100;
        if (year < 0)
            year = 0;
        *value = to_bcd(year);
        break;
    }
    case kRtcRegisterA:
        // Ensure that UIP is 0. Other values (clock frequency) are obsolete.
        *value = 0;
        break;
    case kRtcRegisterB:
        *value = kRtcRegisterBHourFormat;
        if (tm.tm_isdst)
            *value |= kRtcRegisterBDaylightSavings;
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t I8042Handler::Init(Guest* guest) {
    zx_status_t status = guest->CreateMapping(TrapType::PIO_SYNC, I8042_BASE + kI8042DataPort,
                                              1, kI8042DataPort, this);
    if (status != ZX_OK)
        return status;

    return guest->CreateMapping(TrapType::PIO_SYNC, I8042_BASE + kI8042CommandPort,
                                1, kI8042CommandPort, this);
}

zx_status_t I8042Handler::Read(uint64_t port, IoValue* value) {
    switch (port) {
    case kI8042DataPort: {
        value->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        value->u8 = command_ == kI8042CommandTest ? kI8042DataTestResponse : 0;
        break;
    }
    case kI8042CommandPort:
        value->access_size = 1;
        value->u8 = kI8042StatusOutputFull;
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t I8042Handler::Write(uint64_t port, const IoValue& value) {
    switch (port) {
    case kI8042DataPort:
    case kI8042CommandPort: {
        if (value.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        fbl::AutoLock lock(&mutex_);
        command_ = value.u8;
        break;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

zx_status_t IoPort::Init(Guest* guest) {
    zx_status_t status;
    status = pic1_.Init(guest, PIC1_BASE);
    if (status != ZX_OK)
        return status;
    status = pic2_.Init(guest, PIC2_BASE);
    if (status != ZX_OK)
        return status;
    status = pit_.Init(guest);
    if (status != ZX_OK)
        return status;
    status = pm1_.Init(guest);
    if (status != ZX_OK)
        return status;
    status = rtc_.Init(guest);
    if (status != ZX_OK)
        return status;
    status = i8042_.Init(guest);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}
