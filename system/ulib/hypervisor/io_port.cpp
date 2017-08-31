// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <time.h>

#include <hypervisor/address.h>
#include <hypervisor/io_port.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/syscalls/port.h>

#include "acpi_priv.h"

// clang-format off

/* PIC configuration constants. */
#define PIC_INVALID                     UINT8_MAX

/* RTC register addresses. */
#define RTC_REGISTER_SECONDS            0u
#define RTC_REGISTER_MINUTES            2u
#define RTC_REGISTER_HOURS              4u
#define RTC_REGISTER_DAY_OF_MONTH       7u
#define RTC_REGISTER_MONTH              8u
#define RTC_REGISTER_YEAR               9u
#define RTC_REGISTER_A                  10u
#define RTC_REGISTER_B                  11u

/* RTC register B flags. */
#define RTC_REGISTER_B_DAYLIGHT_SAVINGS (1u << 0)
#define RTC_REGISTER_B_HOUR_FORMAT      (1u << 1)

/* I8042 status flags. */
#define I8042_STATUS_OUTPUT_FULL        (1u << 0)
#define I8042_STATUS_INPUT_FULL         (1u << 1)

/* I8042 test constants. */
#define I8042_COMMAND_TEST              0xaa
#define I8042_DATA_TEST_RESPONSE        0x55

// clang-format on

void io_port_init(io_port_t* io_port) {
    memset(io_port, 0, sizeof(*io_port));
}

static uint8_t to_bcd(int binary) {
    return static_cast<uint8_t>(((binary / 10) << 4) | (binary % 10));
}

static mx_status_t handle_rtc(uint8_t rtc_index, uint8_t* value) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL)
        return MX_ERR_INTERNAL;
    switch (rtc_index) {
    case RTC_REGISTER_SECONDS:
        *value = to_bcd(tm.tm_sec);
        break;
    case RTC_REGISTER_MINUTES:
        *value = to_bcd(tm.tm_min);
        break;
    case RTC_REGISTER_HOURS:
        *value = to_bcd(tm.tm_hour);
        break;
    case RTC_REGISTER_DAY_OF_MONTH:
        *value = to_bcd(tm.tm_mday);
        break;
    case RTC_REGISTER_MONTH:
        *value = to_bcd(tm.tm_mon);
        break;
    case RTC_REGISTER_YEAR: {
        // RTC expects the number of years since 2000.
        int year = tm.tm_year - 100;
        if (year < 0)
            year = 0;
        *value = to_bcd(year);
        break;
    }
    case RTC_REGISTER_A:
        // Ensure that UIP is 0. Other values (clock frequency) are obsolete.
        *value = 0;
        break;
    case RTC_REGISTER_B:
        *value = RTC_REGISTER_B_HOUR_FORMAT;
        if (tm.tm_isdst)
            *value |= RTC_REGISTER_B_DAYLIGHT_SAVINGS;
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
}

mx_status_t io_port_read(const io_port_t* io_port, uint16_t port, mx_vcpu_io_t* vcpu_io) {
#ifdef __x86_64__
    switch (port) {
    case RTC_DATA_PORT: {
        vcpu_io->access_size = 1;
        mtx_lock((mtx_t*)&io_port->mutex);
        uint8_t rtc_index = io_port->rtc_index;
        mtx_unlock((mtx_t*)&io_port->mutex);
        return handle_rtc(rtc_index, &vcpu_io->u8);
    }
    case I8042_DATA_PORT:
        vcpu_io->access_size = 1;
        mtx_lock((mtx_t*)&io_port->mutex);
        vcpu_io->u8 = io_port->i8042_command == I8042_COMMAND_TEST ? I8042_DATA_TEST_RESPONSE : 0;
        mtx_unlock((mtx_t*)&io_port->mutex);
        break;
    case I8042_COMMAND_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = I8042_STATUS_OUTPUT_FULL;
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
        vcpu_io->access_size = 2;
        vcpu_io->u16 = 0;
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        vcpu_io->access_size = 2;
        mtx_lock((mtx_t*)&io_port->mutex);
        vcpu_io->u16 = io_port->pm1_enable;
        mtx_unlock((mtx_t*)&io_port->mutex);
        break;
    case PIC1_DATA_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = PIC_INVALID;
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

mx_status_t io_port_write(io_port_t* io_port, const mx_packet_guest_io_t* io) {
#ifdef __x86_64__
    switch (io->port) {
    case I8042_DATA_PORT:
    case I8253_CHANNEL_0:
    case I8253_CONTROL_PORT:
    case PIC1_COMMAND_PORT ... PIC1_DATA_PORT:
    case PIC2_COMMAND_PORT ... PIC2_DATA_PORT:
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
        break;
    case I8042_COMMAND_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&io_port->mutex);
        io_port->i8042_command = io->u8;
        mtx_unlock(&io_port->mutex);
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&io_port->mutex);
        io_port->pm1_enable = io->u16;
        mtx_unlock(&io_port->mutex);
        break;
    case RTC_INDEX_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&io_port->mutex);
        io_port->rtc_index = io->u8;
        mtx_unlock(&io_port->mutex);
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}
