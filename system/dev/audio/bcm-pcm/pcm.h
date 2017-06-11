// Copyright 2017 The Fuchsia Authors
// All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off

#pragma once

#include <stdint.h>
#include <magenta/compiler.h>

typedef volatile struct {

    uint32_t    cs;
    uint32_t    fifo;
    uint32_t    mode;
    uint32_t    rxc;
    uint32_t    txc;
    uint32_t    dreq_lvl;
    uint32_t    inten;
    uint32_t    intstc;
    uint32_t    gray;

} __PACKED bcm_pcm_regs_t;

#define BCM_PCM_STATE_SHUTDOWN          (uint32_t)(0)
#define BCM_PCM_STATE_CLIENT_ACTIVE     (uint32_t)( 1 << 0 )
#define BCM_PCM_STATE_RB_ACTIVE         (uint32_t)( 1 << 1 )
#define BCM_PCM_STATE_RUNNING           (uint32_t)( 1 << 2 )
#define BCM_PCM_STATE_SHUTTING_DOWN     (uint32_t)( 1 << 3 )

#define BCM_PCM_MODE_INITIAL_STATE      (uint32_t)(0)
#define BCM_PCM_TXC_INITIAL_STATE       (uint32_t)(0)
#define BCM_PCM_RXC_INITIAL_STATE       (uint32_t)(0)
#define BCM_PCM_DREQ_LVL_INITIAL_STATE  (uint32_t)((0x20)       | (0x30 << 8) | \
                                                   (0x30 << 16) | (0x10 <<24) )
#define BCM_PCM_CS_INITIAL_STATE        (uint32_t)(0)

#define BCM_PCM_CS_ENABLE               (uint32_t)(0x00000001)
#define BCM_PCM_CS_TXW                  (uint32_t)( 1 << 17)
#define BCM_PCM_CS_RXERR                (uint32_t)( 1 << 16)
#define BCM_PCM_CS_TXERR                (uint32_t)( 1 << 15)
#define BCM_PCM_CS_DMAEN                (uint32_t)( 1 << 9 )
#define BCM_PCM_CS_TXTHR                (uint32_t)( 1 << 5 )         // Set when less than full
#define BCM_PCM_CS_TXCLR                (uint32_t)( 1 << 3 )
#define BCM_PCM_CS_RXCLR                (uint32_t)( 1 << 4 )
#define BCM_PCM_CS_TXON                 (uint32_t)( 1 << 2 )

#define BCM_PCM_MODE_FTXP               (uint32_t)( 1  << 24)
#define BCM_PCM_MODE_CLKI               (uint32_t)( 1  << 22)
#define BCM_PCM_MODE_FLEN_64            (uint32_t)( 63 << 10)
#define BCM_PCM_MODE_FLEN_48            (uint32_t)( 47 << 10)
#define BCM_PCM_MODE_FLEN_32            (uint32_t)( 31 << 10)

#define BCM_PCM_MODE_FSLEN_32           (uint32_t)( 32 <<  0)

#define BCM_PCM_MODE_I2S_16BIT_64BCLK   (uint32_t)( BCM_PCM_MODE_FLEN_64  | \
                                                    BCM_PCM_MODE_FSLEN_32 | \
                                                    BCM_PCM_MODE_FTXP     | \
                                                    BCM_PCM_MODE_CLKI     )

#define BCM_PCM_TXC_CH1EN               (uint32_t)( 1 << 30 )
#define BCM_PCM_TXC_CH2EN               (uint32_t)( 1 << 14 )
#define BCM_PCM_TXC_CH1WID_16           (uint32_t)( 8 << 16 )
#define BCM_PCM_TXC_CH2WID_16           (uint32_t)( 8 <<  0 )

#define BCM_PCM_TXC_I2S_16BIT_64BCLK    ( BCM_PCM_TXC_CH1WID_16 | \
                                          BCM_PCM_TXC_CH2WID_16 | \
                                          BCM_PCM_TXC_CH1EN     | \
                                          BCM_PCM_TXC_CH2EN     | \
                                          (uint32_t)(1 << 20)   | \
                                          (uint32_t)(33 << 4)   )

