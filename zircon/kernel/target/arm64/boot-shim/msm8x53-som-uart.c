// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>
#include <zircon/compiler.h>

#include "debug.h"

#define UART_DM_N0_CHARS_FOR_TX 0x0040
#define UART_DM_CR_CMD_RESET_TX_READY (3 << 8)

#define UART_DM_SR 0x00A4
#define UART_DM_SR_TXRDY (1 << 2)
#define UART_DM_SR_TXEMT (1 << 3)

#define UART_DM_TF 0x0100

#define UARTREG(reg) (*(volatile uint32_t*)(0x078af000 + (reg)))

void uart_pputc(char c) {
  while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXEMT)) {
    ;
  }
  UARTREG(UART_DM_N0_CHARS_FOR_TX) = UART_DM_CR_CMD_RESET_TX_READY;
  UARTREG(UART_DM_N0_CHARS_FOR_TX) = 1;
  __UNUSED uint32_t foo = UARTREG(UART_DM_N0_CHARS_FOR_TX);

  // wait for TX ready
  while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXRDY))
    ;

  UARTREG(UART_DM_TF) = c;

  // wait for TX ready
  while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXRDY))
    ;
}
