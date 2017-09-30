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

#define EXPECTED_INT 43u

static void stub_io_apic(IoApic* io_apic) {
    static LocalApic::Registers local_apic_regs;
    static LocalApic local_apic(ZX_HANDLE_INVALID, reinterpret_cast<uintptr_t>(&local_apic_regs));
    memset(&local_apic_regs, 0, sizeof(local_apic_regs));
    io_apic->RegisterLocalApic(0, &local_apic);
}

static void io_apic_init_with_vector(IoApic* io_apic) {
    stub_io_apic(io_apic);
    IoApic::RedirectEntry entry = {
        .upper = 0,
        .lower = EXPECTED_INT,
    };
    io_apic->SetRedirect(X86_INT_UART, entry);
}

static zx_status_t ok_raise_interrupt(zx_handle_t vcpu, uint32_t vector) {
    return vector == EXPECTED_INT ? ZX_OK : ZX_ERR_INTERNAL;
}

static zx_status_t fail_raise_interrupt(zx_handle_t vcpu, uint32_t vector) {
    return ZX_ERR_BAD_STATE;
}

static bool irq_redirect(void) {
    BEGIN_TEST;

    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        // Interrupts cannot be raised unless the UART IRQ redirect is in place.
        stub_io_apic(&io_apic);

        IoValue io_value = {};
        io_value.access_size = 1;
        io_value.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK, "");
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE, "");
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &ok_raise_interrupt);
        // Interrupts can be raised after the UART IRQ redirect is in place.
        io_apic_init_with_vector(&io_apic);

        IoValue io_value = {};
        io_value.access_size = 1;
        io_value.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK, "");
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY, "");
    }
    END_TEST;
}

// Test behaviour of reads to the Interrupt Identification Register.
static bool read_iir(void) {
    BEGIN_TEST;
    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        // If interrupt id is thr, it should be cleared to none
        uart.set_interrupt_id(UART_INTERRUPT_ID_THR_EMPTY);

        IoValue io_value = {};
        zx_status_t status = uart.Read(UART_INTERRUPT_ID_PORT, &io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(io_value.access_size, 1);
        ASSERT_EQ(io_value.u8, UART_INTERRUPT_ID_THR_EMPTY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE);
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        // If interrupt id is not thr, it should be left alone.
        uart.set_interrupt_id(UART_INTERRUPT_ID_RDA);

        IoValue io_value = {};
        zx_status_t status = uart.Read(UART_INTERRUPT_ID_PORT, &io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(io_value.access_size, 1);
        ASSERT_EQ(io_value.u8, UART_INTERRUPT_ID_RDA);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_RDA);
    }
    END_TEST;
}

// Test behaviour of reads from the Receive Buffer Register.
static bool read_rbr(void) {
    BEGIN_TEST;

    {
        IoApic io_apic;
        Uart uart(&io_apic, &ok_raise_interrupt);
        // Reads from RBR should unset UART_LINE_STATUS_DATA_READY,
        // clear interrupt status and trigger further interrupts if available.
        io_apic_init_with_vector(&io_apic);
        uart.set_line_status(UART_LINE_STATUS_THR_EMPTY | UART_LINE_STATUS_DATA_READY);
        uart.set_rx_buffer('a');
        uart.set_interrupt_id(UART_INTERRUPT_ID_RDA);
        uart.set_interrupt_enable(UART_INTERRUPT_ENABLE_THR_EMPTY);

        IoValue io_value = {};
        zx_status_t status = uart.Read(UART_RECEIVE_PORT, &io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(io_value.access_size, 1);
        ASSERT_EQ(io_value.u8, 'a');
        ASSERT_EQ(uart.rx_buffer(), 0);
        ASSERT_EQ(uart.line_status(), UART_LINE_STATUS_THR_EMPTY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY);
    }
    {
        // If interrupt_id was not RDA, it should not be cleared.
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        uart.set_interrupt_id(UART_INTERRUPT_ID_THR_EMPTY);
        uart.set_interrupt_enable(UART_INTERRUPT_ENABLE_NONE);

        IoValue io_value = {};
        zx_status_t status = uart.Read(UART_RECEIVE_PORT, &io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY);
    }
    END_TEST;
}

// Test behaviour of writes to the Interrupt Enable Register.
static bool write_ier(void) {
    BEGIN_TEST;

    {
        // Setting IER when divisor latch is on should be a no-op
        IoApic io_apic;
        Uart uart(&io_apic, &ok_raise_interrupt);
        stub_io_apic(&io_apic);
        uart.set_line_control(UART_LINE_CONTROL_DIV_LATCH);
        uart.set_interrupt_enable(0);

        IoValue io_value = {};
        io_value.access_size = 1;
        io_value.u8 = UART_INTERRUPT_ENABLE_RDA;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_enable(), 0);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE); // should be untouched
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        // Setting anything not THR enable shouldn't trigger any interrupts.
        stub_io_apic(&io_apic);
        // Only UART_INTERRUPT_ENABLE_THR_EMPTY should trigger interrupts on IER write.
        // Anything else should not.
        io_apic_init_with_vector(&io_apic);

        IoValue io_value = {};
        io_value.access_size = 1;
        io_value.u8 = UART_INTERRUPT_ENABLE_RDA;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_enable(), UART_INTERRUPT_ENABLE_RDA);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE); // should be untouched
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        // UART_INTERRUPT_ID_THR_EMPTY should not be raised if
        // line status is not UART_LINE_STATUS_THR_EMPTY.
        io_apic_init_with_vector(&io_apic);
        uart.set_line_status(UART_LINE_STATUS_DATA_READY);

        IoValue io_value = {};
        io_value.access_size = 1;
        // THR enable should trigger a THR interrupt
        io_value.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_enable(), UART_INTERRUPT_ENABLE_THR_EMPTY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE); // should be untouched
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &ok_raise_interrupt);
        // Setting UART_INTERRUPT_ENABLE_THR_EMPTY should trigger UART_INTERRUPT_ID_THR_EMPTY
        // if line status is UART_LINE_STATUS_THR_EMPTY.
        io_apic_init_with_vector(&io_apic);

        IoValue io_value = {};
        io_value.access_size = 1;
        // THR enable should trigger a THR interrupt
        io_value.u8 = UART_INTERRUPT_ENABLE_THR_EMPTY;

        zx_status_t status = uart.Write(UART_INTERRUPT_ENABLE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_enable(), UART_INTERRUPT_ENABLE_THR_EMPTY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY);
    }

    END_TEST;
}

// Test behaviour of writes to the Transmit Holding Register
static bool write_thr(void) {
    BEGIN_TEST;

    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        io_apic_init_with_vector(&io_apic);
        uart.set_line_status(UART_LINE_STATUS_DATA_READY);
        uart.set_interrupt_enable(UART_INTERRUPT_ENABLE_NONE);

        // If divisor latch is enabled, this should be a no-op, so interrupt_id
        // should remain the same.
        uart.set_line_control(UART_LINE_CONTROL_DIV_LATCH);
        uart.set_interrupt_id(UART_INTERRUPT_ID_THR_EMPTY);

        IoValue io_value;
        io_value.u8 = 0x1;
        io_value.access_size = 1;

        zx_status_t status = uart.Write(UART_RECEIVE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY);
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &fail_raise_interrupt);
        io_apic_init_with_vector(&io_apic);
        // If this was responding to a THR empty interrupt, IIR should be reset
        // on THR write.
        uart.set_interrupt_id(UART_INTERRUPT_ID_THR_EMPTY);
        uart.set_line_status(UART_LINE_STATUS_DATA_READY);
        uart.set_interrupt_enable(UART_INTERRUPT_ENABLE_NONE);

        IoValue io_value;
        io_value.access_size = 3;
        io_value.data[0] = 0x75;
        io_value.data[1] = 0x61;
        io_value.data[2] = 0x0d;

        zx_status_t status = uart.Write(UART_RECEIVE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.line_status(), UART_LINE_STATUS_THR_EMPTY | UART_LINE_STATUS_DATA_READY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_NONE);
    }
    {
        IoApic io_apic;
        Uart uart(&io_apic, &ok_raise_interrupt);
        io_apic_init_with_vector(&io_apic);
        uart.set_line_status(UART_LINE_STATUS_DATA_READY);

        // If THR empty interrupts are enabled, an interrupt should be raised.
        uart.set_interrupt_enable(UART_INTERRUPT_ENABLE_THR_EMPTY);
        uart.set_interrupt_id(UART_INTERRUPT_ID_NONE);

        IoValue io_value;
        io_value.access_size = 3;
        io_value.data[0] = 0x72;
        io_value.data[1] = 0x74;
        io_value.data[2] = 0x0d;

        zx_status_t status = uart.Write(UART_RECEIVE_PORT, io_value);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(uart.line_status(), UART_LINE_STATUS_THR_EMPTY | UART_LINE_STATUS_DATA_READY);
        ASSERT_EQ(uart.interrupt_id(), UART_INTERRUPT_ID_THR_EMPTY);
    }
    END_TEST;
}

BEGIN_TEST_CASE(uart)
RUN_TEST(irq_redirect)
RUN_TEST(read_rbr)
RUN_TEST(read_iir)
RUN_TEST(write_ier)
RUN_TEST(write_thr)
END_TEST_CASE(uart)
