// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/address.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <unittest/unittest.h>

#define EXPECTED_INT    43u

static void stub_io_apic(io_apic_t* io_apic) {
    static local_apic_t local_apic = {};
    local_apic.vcpu = MX_HANDLE_INVALID;
    io_apic_init(io_apic);
    io_apic->local_apic[0] = &local_apic;
}

static bool read_iir(void) {
    BEGIN_TEST;
    {
        uart_t uart = {};
        // If interrupt id is thr, it should be cleared to none
        uart.interrupt_id = UART_INTERRUPT_ID_THR_EMPTY;
        mx_vcpu_io_t vcpu_io;

        mx_status_t status = uart_read(&uart, UART_INTERRUPT_ID_PORT, &vcpu_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(vcpu_io.access_size, 1, "");
        ASSERT_EQ(vcpu_io.u8, UART_INTERRUPT_ID_THR_EMPTY, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_NONE, "");
    }
    {
        uart_t uart = {};
        // If interrupt id is not thr, it should be left alone.
        uart.interrupt_id = UART_INTERRUPT_ID_RDA;
        mx_vcpu_io_t vcpu_io;

        mx_status_t status = uart_read(&uart, UART_INTERRUPT_ID_PORT, &vcpu_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(vcpu_io.access_size, 1, "");
        ASSERT_EQ(vcpu_io.u8, UART_INTERRUPT_ID_RDA, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_RDA, "");
    }
    END_TEST;
}

static mx_status_t dummy_raise_interrupt(mx_handle_t vcpu, uint32_t vector) {
  return vector == EXPECTED_INT ? MX_OK : MX_ERR_INTERNAL;
}

static bool write_ier(void) {
    BEGIN_TEST;

    uart_t uart;
    io_apic_t io_apic;
    {
        // Setting anything not THR enable shouldn't trigger any interrupts.
        stub_io_apic(&io_apic);
        uart_init(&uart, &io_apic);

        mx_packet_guest_io_t guest_io = {};
        guest_io.port = UART_INTERRUPT_ENABLE_PORT;
        guest_io.access_size = 1;
        guest_io.u8 = UART_INTERRUPT_ENABLE_RDA;

        mx_status_t status = uart_write(&uart, &guest_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(uart.interrupt_enable, UART_INTERRUPT_ENABLE_RDA, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_NONE, ""); // should be untouched
    }
    {
        // Setting THR enabled should not trigger a THR interrupt
        // because the ioapic redirect has not occurred.
        stub_io_apic(&io_apic);
        uart_init(&uart, &io_apic);

        mx_packet_guest_io_t guest_io = {};
        guest_io.port = UART_INTERRUPT_ENABLE_PORT;
        guest_io.access_size = 1;
        // THR enable should trigger a THR interrupt
        guest_io.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        mx_status_t status = uart_write(&uart, &guest_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(uart.interrupt_enable, UART_INTERRUPT_ENABLE_THR_EMPTY, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_NONE, ""); // should be untouched
    }
    {
        // Setting THR enabled should trigger a THR interrupt
        // because the ioapic redirect has occurred.
        stub_io_apic(&io_apic);
        io_apic.redirect[X86_INT_UART * 2] = EXPECTED_INT;
        uart_init(&uart, &io_apic);
        uart.raise_interrupt = dummy_raise_interrupt;

        mx_packet_guest_io_t guest_io = {};
        guest_io.port = UART_INTERRUPT_ENABLE_PORT;
        guest_io.access_size = 1;
        // THR enable should trigger a THR interrupt
        guest_io.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        mx_status_t status = uart_write(&uart, &guest_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(uart.interrupt_enable, UART_INTERRUPT_ENABLE_THR_EMPTY, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_THR_EMPTY, "");
    }
    END_TEST;
}

static bool write_thr(void) {
    BEGIN_TEST;

    uart_t uart;
    io_apic_t io_apic;
    {
        stub_io_apic(&io_apic);
        uart_init(&uart, &io_apic);

        // If this was responding to a THR empty interrupt, IIR should be reset
        // on THR write.
        uart.interrupt_id = UART_INTERRUPT_ID_THR_EMPTY;
        mx_packet_guest_io_t guest_io = {};
        guest_io.port = UART_RECEIVE_PORT;
        guest_io.access_size = 3;
        guest_io.data[0] = 0x75;
        guest_io.data[1] = 0x61;
        guest_io.data[2] = 0x0d;

        mx_status_t status = uart_write(&uart, &guest_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(uart.line_status, UART_THR_EMPTY, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_NONE, "");
    }
    {
        stub_io_apic(&io_apic);
        io_apic.redirect[X86_INT_UART * 2] = EXPECTED_INT;
        uart_init(&uart, &io_apic);
        uart.raise_interrupt = dummy_raise_interrupt;

        // If THR empty interrupts are enabled, an interrupt should be raised.
        uart.interrupt_enable = UART_INTERRUPT_ENABLE_THR_EMPTY;
        uart.interrupt_id = UART_INTERRUPT_ID_NONE;
        mx_packet_guest_io_t guest_io = {};
        guest_io.port = UART_RECEIVE_PORT;
        guest_io.access_size = 3;
        guest_io.data[0] = 0x72;
        guest_io.data[1] = 0x74;
        guest_io.data[2] = 0x0d;

        mx_status_t status = uart_write(&uart, &guest_io);
        ASSERT_EQ(status, MX_OK, "");
        ASSERT_EQ(uart.line_status, UART_THR_EMPTY, "");
        ASSERT_EQ(uart.interrupt_id, UART_INTERRUPT_ID_THR_EMPTY, "");
    }
    END_TEST;
}

BEGIN_TEST_CASE(uart)
RUN_TEST(read_iir)
RUN_TEST(write_ier)
RUN_TEST(write_thr)
END_TEST_CASE(uart)
