// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>
#include "debug.h"

static volatile uint32_t* aux_mu_io_reg = (uint32_t*)0xfe215040;  // Mini UART I/O Data
static volatile uint32_t* aux_mu_lsr_reg = (uint32_t*)0xfe215054; // Mini UART Line Status 

void uart_pputc(char c) {

  /* spin while fifo is full */
  while (!(*aux_mu_lsr_reg & (1 << 5)))		// while Transmitter not empty
    ;
    
  *aux_mu_io_reg = c;
}
