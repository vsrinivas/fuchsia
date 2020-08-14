/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BCDC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BCDC_H_

#include "core.h"
#include "fwil_types.h"
#include "netbuf.h"

// The bcme_status_t was introduced to typecheck handling of firmware error codes.
// Because the firmware uses brcmf_proto_bcdc_dcmd directly, we must ensure
// the size of bcme_status_t is the same as int32_t which was the original type
// of the brcmf_proto_bcdc_dcmd status field.
static_assert(sizeof(int32_t) == sizeof(bcme_status_t),
              "bcme_status_t is not the same size as int32_t");
struct brcmf_proto_bcdc_dcmd {
  uint32_t cmd;         /* dongle command value */
  uint32_t len;         /* input/output buflen (excludes header) */
  uint32_t flags;       /* flag defns given below */
  bcme_status_t status; /* status code returned from the device */
};

/**
 * struct brcmf_proto_bcdc_header - BCDC header format
 *
 * @flags: flags contain protocol and checksum info.
 * @priority: 802.1d priority and USB flow control info (bit 4:7).
 * @flags2: additional flags containing dongle interface index.
 * @data_offset: start of packet data. header is following by firmware signals.
 */
struct brcmf_proto_bcdc_header {
  uint8_t flags;
  uint8_t priority;
  uint8_t flags2;
  uint8_t data_offset;
};

/* Must be atleast SDPCM_RESERVE
 * (amount of header tha might be added)
 * plus any space that might be needed
 * for bus alignment padding.
 */
#define BUS_HEADER_LEN (16 + 64)
#define BCDC_TX_IOCTL_MAX_MSG_SIZE \
  (BRCMF_TX_IOCTL_MAX_MSG_SIZE - sizeof(struct brcmf_proto_bcdc_dcmd))
struct brcmf_bcdc {
  uint16_t reqid;
  uint8_t bus_header[BUS_HEADER_LEN];
  struct brcmf_proto_bcdc_dcmd msg;
  // buf must be packed right after msg; see brcmf_proto_bcdc_msg
  unsigned char buf[BRCMF_DCMD_MAXLEN];
  struct brcmf_fws_info* fws;
};

// clang-format off

/* BCDC flag definitions */
#define BCDC_DCMD_ERROR     0x00000001       /* 1=cmd failed */
#define BCDC_DCMD_SET       0x00000002       /* 0=get, 1=set cmd */
#define BCDC_DCMD_IF_MASK   0x0000F000       /* I/F index */
#define BCDC_DCMD_IF_SHIFT  12
#define BCDC_DCMD_ID_MASK   0xFFFF0000       /* id an cmd pairing */
#define BCDC_DCMD_ID_SHIFT  16               /* ID Mask shift bits */
#define BCDC_DCMD_ID(flags) (((flags)&BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT)
#define BCDC_DCMD_IFIDX(flags) (((flags)&BCDC_DCMD_IF_MASK) >> BCDC_DCMD_IF_SHIFT)

/*
 * BCDC header - Broadcom specific extension of CDC.
 * Used on data packets to convey priority across USB.
 */
#define BCDC_HEADER_LEN      4
#define BCDC_PROTO_VER       2          /* Protocol version */
#define BCDC_FLAG_VER_MASK   0xf0       /* Protocol version mask */
#define BCDC_FLAG_VER_SHIFT  4          /* Protocol version shift */
#define BCDC_FLAG_SUM_GOOD   0x04       /* Good RX checksums */
#define BCDC_FLAG_SUM_NEEDED 0x08       /* Dongle needs to do TX checksums */
#define BCDC_PRIORITY_MASK   0x07
#define BCDC_FLAG2_IF_MASK   0x0f       /* packet rx interface in APSTA */
#define BCDC_FLAG2_IF_SHIFT  0
#define BCDC_GET_IF_IDX(hdr) ((int)((((hdr)->flags2) & BCDC_FLAG2_IF_MASK) >> BCDC_FLAG2_IF_SHIFT))
#define BCDC_SET_IF_IDX(hdr, idx) \
  ((hdr)->flags2 = (((hdr)->flags2 & ~BCDC_FLAG2_IF_MASK) | ((idx) << BCDC_FLAG2_IF_SHIFT)))

// clang-format on

zx_status_t brcmf_proto_bcdc_attach(brcmf_pub* drvr);
void brcmf_proto_bcdc_detach(brcmf_pub* drvr);
void brcmf_proto_bcdc_txflowblock(brcmf_pub* drvr, bool state);
void brcmf_proto_bcdc_txcomplete(brcmf_pub* drvr, brcmf_netbuf* txp, bool success);
struct brcmf_fws_info* drvr_to_fws(brcmf_pub* drvr);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BCDC_H_
