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
#include "device.h"
#include "netbuf.h"

struct brcmf_proto_bcdc_dcmd {
  uint32_t cmd;    /* dongle command value */
  uint32_t len;    /* input/output buflen (excludes header) */
  uint32_t flags;  /* flag defns given below */
  int32_t status;  /* status code returned from the device */
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

// clang-format on

zx_status_t brcmf_proto_bcdc_attach(struct brcmf_pub* drvr);
void brcmf_proto_bcdc_detach(struct brcmf_pub* drvr);
void brcmf_proto_bcdc_txflowblock(struct brcmf_device* dev, bool state);
void brcmf_proto_bcdc_txcomplete(struct brcmf_device* dev, struct brcmf_netbuf* txp, bool success);
struct brcmf_fws_info* drvr_to_fws(struct brcmf_pub* drvr);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BCDC_H_
