/*
 * Copyright (c) 2012 Broadcom Corporation
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWSIGNAL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWSIGNAL_H_

#include <zircon/types.h>

#include "core.h"
#include "linuxisms.h"
#include "netbuf.h"

/**
 * DOC: Firmware Signalling
 *
 * Firmware can send signals to host and vice versa, which are passed in the
 * data packets using TLV based header. This signalling layer is on top of the
 * BDC bus protocol layer.
 */
#define BRCMF_FWS_FLAGS_RSSI_SIGNALS 0x0001
#define BRCMF_FWS_FLAGS_XONXOFF_SIGNALS 0x0002
#define BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS 0x0004
#define BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE 0x0008
#define BRCMF_FWS_FLAGS_PSQ_GENERATIONFSM_ENABLE 0x0010
#define BRCMF_FWS_FLAGS_PSQ_ZERO_BUFFER_ENABLE 0x0020
#define BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE 0x0040

/*
 * single definition for firmware-driver flow control tlv's.
 *
 * each tlv is specified by BRCMF_FWS_TLV_DEF(name, ID, length).
 * A length value 0 indicates variable length tlv.
 */
#define BRCMF_FWS_TLV_DEFLIST                    \
  BRCMF_FWS_TLV_DEF(MAC_OPEN, 1, 1)              \
  BRCMF_FWS_TLV_DEF(MAC_CLOSE, 2, 1)             \
  BRCMF_FWS_TLV_DEF(MAC_REQUEST_CREDIT, 3, 2)    \
  BRCMF_FWS_TLV_DEF(TXSTATUS, 4, 4)              \
  BRCMF_FWS_TLV_DEF(PKTTAG, 5, 4)                \
  BRCMF_FWS_TLV_DEF(MACDESC_ADD, 6, 8)           \
  BRCMF_FWS_TLV_DEF(MACDESC_DEL, 7, 8)           \
  BRCMF_FWS_TLV_DEF(RSSI, 8, 1)                  \
  BRCMF_FWS_TLV_DEF(INTERFACE_OPEN, 9, 1)        \
  BRCMF_FWS_TLV_DEF(INTERFACE_CLOSE, 10, 1)      \
  BRCMF_FWS_TLV_DEF(FIFO_CREDITBACK, 11, 6)      \
  BRCMF_FWS_TLV_DEF(PENDING_TRAFFIC_BMP, 12, 2)  \
  BRCMF_FWS_TLV_DEF(MAC_REQUEST_PACKET, 13, 3)   \
  BRCMF_FWS_TLV_DEF(HOST_REORDER_RXPKTS, 14, 10) \
  BRCMF_FWS_TLV_DEF(TRANS_ID, 18, 6)             \
  BRCMF_FWS_TLV_DEF(COMP_TXSTATUS, 19, 1)        \
  BRCMF_FWS_TLV_DEF(FILLER, 255, 0)

#define FWS_TLV_TYPE_SIZE 1
#define FWS_TLV_LEN_SIZE 1
#define FWS_TLV_TYPE_OFFSET 0
#define FWS_TLV_LEN_OFFSET (FWS_TLV_TYPE_OFFSET + FWS_TLV_TYPE_SIZE)
#define FWS_TLV_DATA_OFFSET (FWS_TLV_LEN_OFFSET + FWS_TLV_LEN_SIZE)
#define FWS_RSSI_DATA_LEN 1
/*
 * enum brcmf_fws_tlv_type - definition of tlv identifiers.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) BRCMF_FWS_TYPE_##name = id,
enum brcmf_fws_tlv_type { BRCMF_FWS_TLV_DEFLIST BRCMF_FWS_TYPE_INVALID };
#undef BRCMF_FWS_TLV_DEF

zx_status_t brcmf_fws_attach(struct brcmf_pub* drvr, struct brcmf_fws_info** fws_out);
void brcmf_fws_detach(struct brcmf_fws_info* fws);
bool brcmf_fws_queue_netbufs(struct brcmf_fws_info* fws);
bool brcmf_fws_fc_active(struct brcmf_fws_info* fws);
void brcmf_fws_hdrpull(struct brcmf_if* ifp, int16_t siglen, struct brcmf_netbuf* netbuf);
zx_status_t brcmf_fws_process_netbuf(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf);

void brcmf_fws_reset_interface(struct brcmf_if* ifp);
void brcmf_fws_add_interface(struct brcmf_if* ifp);
void brcmf_fws_del_interface(struct brcmf_if* ifp);
void brcmf_fws_bustxfail(struct brcmf_fws_info* fws, struct brcmf_netbuf* netbuf);
void brcmf_fws_bus_blocked(struct brcmf_pub* drvr, bool flow_blocked);
void brcmf_fws_rxreorder(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWSIGNAL_H_
