// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <dev/driver.h>
#include <dev/class/block.h>
#include <dev/class/netif.h>
#include <platform/uart.h>
#include <platform/ide.h>
#include <platform/pcnet.h>
#include <platform.h>
#include <malloc.h>
#include <string.h>
#include <debug.h>

#if 0
static const struct platform_uart_config uart0_config = {
    .io_port = 0x3f8,
    .irq = ISA_IRQ_SERIAL1,
    .baud_rate = 115200,
    .rx_buf_len = 1024,
    .tx_buf_len = 1024,
};

DEVICE_INSTANCE(uart, uart0, &uart0_config);
#endif

#ifndef ARCH_X86_64
static const struct platform_ide_config ide0_config = {
};

DEVICE_INSTANCE(ide, ide0, &ide0_config);

#endif

void target_init(void)
{
    //device_init_all();
}

