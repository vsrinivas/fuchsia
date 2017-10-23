// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define FTDI_TYPE_R         (0x0600)
#define FTDI_TYPE_BM        (0x0400)
#define FTDI_TYPE_AM        (0x0200)
#define FTDI_TYPE_2232C     (0x0500)
#define FTDI_TYPE_2232H     (0x0700)
#define FTDI_TYPE_4232H     (0x0800)
#define FTDI_TYPE_232H      (0x0900)

// Clock divisors
#define FTDI_TYPE_R_DIVISOR     (16)

#define FTDI_VID         0x0403
#define FTDI_232R_PID    0x6001
#define FTDI_2232_PID    0x6010

#define FTDI_H_CLK 120000000
#define FTDI_C_CLK  48000000

#define FTDI_SIO_RESET          0 /* Reset the port */
#define FTDI_SIO_MODEM_CTRL     1 /* Set the modem control register */
#define FTDI_SIO_SET_FLOW_CTRL  2 /* Set flow control register */
#define FTDI_SIO_SET_BAUDRATE   3 /* Set baud rate */
#define FTDI_SIO_SET_DATA       4 /* Set the data characteristics of the port */

/* Requests */
#define FTDI_SIO_RESET_REQUEST             FTDI_SIO_RESET
#define FTDI_SIO_SET_BAUDRATE_REQUEST      FTDI_SIO_SET_BAUD_RATE
#define FTDI_SIO_SET_DATA_REQUEST          FTDI_SIO_SET_DATA
#define FTDI_SIO_SET_FLOW_CTRL_REQUEST     FTDI_SIO_SET_FLOW_CTRL
#define FTDI_SIO_SET_MODEM_CTRL_REQUEST    FTDI_SIO_MODEM_CTRL
#define FTDI_SIO_POLL_MODEM_STATUS_REQUEST 0x05
#define FTDI_SIO_SET_EVENT_CHAR_REQUEST    0x06
#define FTDI_SIO_SET_ERROR_CHAR_REQUEST    0x07
#define FTDI_SIO_SET_LATENCY_TIMER_REQUEST 0x09
#define FTDI_SIO_GET_LATENCY_TIMER_REQUEST 0x0A
#define FTDI_SIO_SET_BITMODE_REQUEST       0x0B
#define FTDI_SIO_READ_PINS_REQUEST         0x0C
#define FTDI_SIO_READ_EEPROM_REQUEST       0x90
#define FTDI_SIO_WRITE_EEPROM_REQUEST      0x91
#define FTDI_SIO_ERASE_EEPROM_REQUEST      0x92


