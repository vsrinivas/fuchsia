/*
 * Copyright (c) 2010 Broadcom Corporation
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

/*******************************************************************************
 * Communicates with the dongle by using dcmd codes.
 * For certain dcmd codes, the dongle interprets string data from the host.
 ******************************************************************************/

#include "bcdc.h"

#include <zircon/status.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "fwsignal.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "proto.h"

/*
 * maximum length of firmware signal data between
 * the BCDC header and packet data in the tx path.
 */
#define BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES 12

#define RETRIES 2 /* # of retries to retrieve matching dcmd response */

struct brcmf_fws_info* drvr_to_fws(struct brcmf_pub* drvr) {
  struct brcmf_bcdc* bcdc = static_cast<decltype(bcdc)>(drvr->proto->pd);

  return bcdc->fws;
}

static zx_status_t brcmf_proto_bcdc_msg(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                        uint len, bool set) {
  struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
  struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
  uint32_t flags;
  if (cmd == BRCMF_C_GET_VAR) {
    // buf starts with a NULL-terminated string
    BRCMF_DBG(BCDC, "Getting iovar '%.*s'", len, static_cast<char*>(buf));
  } else if (cmd == BRCMF_C_SET_VAR) {
    // buf starts with a NULL-terminated string
    BRCMF_DBG(BCDC, "Setting iovar '%.*s'", len, static_cast<char*>(buf));
  } else {
    BRCMF_DBG(BCDC, "Enter");
  }

  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(BCDC) && BRCMF_IS_ON(BYTES), buf, len,
                     "Sending BCDC Message (%u bytes)", len);

  memset(msg, 0, sizeof(struct brcmf_proto_bcdc_dcmd));

  msg->cmd = cmd;
  msg->len = len;
  flags = (++bcdc->reqid << BCDC_DCMD_ID_SHIFT);
  if (set) {
    flags |= BCDC_DCMD_SET;
  }
  flags = (flags & ~BCDC_DCMD_IF_MASK) | (ifidx << BCDC_DCMD_IF_SHIFT);
  msg->flags = flags;

  if (buf) {
    memcpy(bcdc->buf, buf, len);
  }

  len += sizeof(*msg);
  if (len > BRCMF_TX_IOCTL_MAX_MSG_SIZE) {
    len = BRCMF_TX_IOCTL_MAX_MSG_SIZE;
  }
  /* Send request */
  return brcmf_bus_txctl(drvr->bus_if, (unsigned char*)&bcdc->msg, len);
}

static zx_status_t brcmf_proto_bcdc_cmplt(struct brcmf_pub* drvr, uint32_t id, uint32_t len,
                                          int* rxlen_out) {
  zx_status_t ret;
  struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;

  BRCMF_DBG(BCDC, "Enter");
  len += sizeof(struct brcmf_proto_bcdc_dcmd);
  do {
    ret = brcmf_bus_rxctl(drvr->bus_if, (unsigned char*)&bcdc->msg, len, rxlen_out);
    if (ret != ZX_OK) {
      break;
    }
  } while (BCDC_DCMD_ID(bcdc->msg.flags) != id);

  uint32_t actual_len = bcdc->msg.len;
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(BCDC) && BRCMF_IS_ON(BYTES), bcdc->buf, actual_len,
                     "Received BCDC Message (%u bytes)", actual_len);

  return ret;
}

static zx_status_t brcmf_proto_bcdc_query_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd,
                                               void* buf, uint len, bcme_status_t* fwerr) {
  struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
  struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
  void* info;
  zx_status_t ret = ZX_OK;
  int retries = 0;
  int rxlen;
  uint32_t id, flags;

  BRCMF_DBG(BCDC, "Enter, cmd %d len %d", cmd, len);

  *fwerr = BCME_OK;
  ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, false);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_proto_bcdc_msg failed w/status %s", zx_status_get_string(ret));
    goto done;
  }

retry:
  /* wait for interrupt and get first fragment */
  ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len, &rxlen);
  if (ret != ZX_OK) {
    goto done;
  }

  flags = msg->flags;
  id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

  if ((id < bcdc->reqid) && (++retries < RETRIES)) {
    goto retry;
  }
  if (id != bcdc->reqid) {
    BRCMF_ERR("%s: unexpected request id %d (expected %d)",
              brcmf_ifname(brcmf_get_ifp(drvr, ifidx)), id, bcdc->reqid);
    ret = ZX_ERR_BAD_STATE;
    goto done;
  }

  /* Check info buffer */
  info = (void*)&bcdc->buf[0];

  /* Copy info buffer */
  if (buf) {
    if (rxlen < (int)len) {
      len = rxlen;
    }
    memcpy(buf, info, len);
  }

  ret = ZX_OK;

  /* Check the ERROR flag */
  if (flags & BCDC_DCMD_ERROR) {
    *fwerr = msg->status;
  }
done:
  return ret;
}

static zx_status_t brcmf_proto_bcdc_set_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                             uint len, bcme_status_t* fwerr) {
  struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
  struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
  zx_status_t ret;
  uint32_t flags, id;
  int rxlen_out;

  BRCMF_DBG(BCDC, "Enter, cmd %d len %d", cmd, len);

  *fwerr = BCME_OK;
  ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, true);
  if (ret != ZX_OK) {
    goto done;
  }

  ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len, &rxlen_out);
  if (ret != ZX_OK) {
    BRCMF_DBG(TEMP, "Just got back from message cmplt, result %d", ret);
    goto done;
  }

  flags = msg->flags;
  id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

  if (id != bcdc->reqid) {
    BRCMF_ERR("%s: unexpected request id %d (expected %d)",
              brcmf_ifname(brcmf_get_ifp(drvr, ifidx)), id, bcdc->reqid);
    ret = ZX_ERR_BAD_STATE;
    goto done;
  }

  ret = ZX_OK;

  /* Check the ERROR flag */
  if (flags & BCDC_DCMD_ERROR) {
    *fwerr = msg->status;
  }

done:
  return ret;
}

static void brcmf_proto_bcdc_hdrpush(struct brcmf_pub* drvr, int ifidx, uint8_t offset,
                                     struct brcmf_netbuf* pktbuf) {
  struct brcmf_proto_bcdc_header* h;

  BRCMF_DBG(BCDC, "Enter");

  /* Push BDC header used to convey priority for buses that don't */
  brcmf_netbuf_grow_head(pktbuf, BCDC_HEADER_LEN);
  h = (struct brcmf_proto_bcdc_header*)(pktbuf->data);

  h->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT);
  if (pktbuf->ip_summed == CHECKSUM_PARTIAL) {
    h->flags |= BCDC_FLAG_SUM_NEEDED;
  }

  h->priority = (pktbuf->priority & BCDC_PRIORITY_MASK);
  h->flags2 = 0;
  h->data_offset = offset;
  BCDC_SET_IF_IDX(h, ifidx);
}

static zx_status_t brcmf_proto_bcdc_hdrpull(struct brcmf_pub* drvr, bool do_fws,
                                            struct brcmf_netbuf* pktbuf, struct brcmf_if** ifp) {
  struct brcmf_proto_bcdc_header* h;
  struct brcmf_if* tmp_if;

  BRCMF_DBG(BCDC, "Enter");

  /* Pop BCDC header used to convey priority for buses that don't */
  if (pktbuf->len <= BCDC_HEADER_LEN) {
    BRCMF_DBG(INFO, "rx data too short (%d <= %d)", pktbuf->len, BCDC_HEADER_LEN);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  h = (struct brcmf_proto_bcdc_header*)(pktbuf->data);

  tmp_if = brcmf_get_ifp(drvr, BCDC_GET_IF_IDX(h));
  if (!tmp_if) {
    BRCMF_DBG(INFO, "no matching ifp found");
    return ZX_ERR_NOT_FOUND;
  }
  if (((h->flags & BCDC_FLAG_VER_MASK) >> BCDC_FLAG_VER_SHIFT) != BCDC_PROTO_VER) {
    BRCMF_ERR("%s: non-BCDC packet received, flags 0x%x", brcmf_ifname(tmp_if), h->flags);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (h->flags & BCDC_FLAG_SUM_GOOD) {
    BRCMF_DBG(BCDC, "%s: BDC rcv, good checksum, flags 0x%x", brcmf_ifname(tmp_if), h->flags);
    pktbuf->ip_summed = CHECKSUM_UNNECESSARY;
  }

  pktbuf->priority = h->priority & BCDC_PRIORITY_MASK;

  brcmf_netbuf_shrink_head(pktbuf, BCDC_HEADER_LEN);
  if (do_fws) {
    brcmf_fws_hdrpull(tmp_if, h->data_offset << 2, pktbuf);
  } else {
    brcmf_netbuf_shrink_head(pktbuf, h->data_offset << 2);
  }

  if (pktbuf->len == 0) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if (ifp != NULL) {
    *ifp = tmp_if;
  }
  return ZX_OK;
}

static zx_status_t brcmf_proto_bcdc_tx_queue_data(struct brcmf_pub* drvr, int ifidx,
                                                  std::unique_ptr<wlan::brcmfmac::Netbuf> netbuf) {
  zx_status_t ret = ZX_OK;
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, ifidx);
  struct brcmf_bcdc* bcdc = static_cast<decltype(bcdc)>(drvr->proto->pd);

  // Copy the Netbuf's data in to a brcmf_netbuf, since that's what the rest of this stack
  // understands.
  struct brcmf_netbuf* b_netbuf = brcmf_netbuf_allocate(netbuf->size() + drvr->hdrlen);
  if (b_netbuf == nullptr) {
    ret = ZX_ERR_NO_MEMORY;
    goto done;
  }

  brcmf_netbuf_grow_tail(b_netbuf, netbuf->size() + drvr->hdrlen);
  brcmf_netbuf_shrink_head(b_netbuf, drvr->hdrlen);
  memcpy(b_netbuf->data, netbuf->data(), netbuf->size());
  b_netbuf->priority = netbuf->priority();

  if (!brcmf_fws_queue_netbufs(bcdc->fws)) {
    ret = brcmf_proto_txdata(drvr, ifidx, 0, b_netbuf);
  } else {
    ret = brcmf_fws_process_netbuf(ifp, b_netbuf);
  }

done:
  if (ret != ZX_OK) {
    brcmu_pkt_buf_free_netbuf(b_netbuf);
  }

  netbuf->Return(ret);
  return ret;
}

static int brcmf_proto_bcdc_txdata(struct brcmf_pub* drvr, int ifidx, uint8_t offset,
                                   struct brcmf_netbuf* pktbuf) {
  brcmf_proto_bcdc_hdrpush(drvr, ifidx, offset, pktbuf);
  return brcmf_bus_txdata(drvr->bus_if, pktbuf);
}

void brcmf_proto_bcdc_txflowblock(brcmf_pub* drvr, bool state) {
  BRCMF_DBG(TRACE, "Enter");
  brcmf_fws_bus_blocked(drvr, state);
}

void brcmf_proto_bcdc_txcomplete(brcmf_pub* drvr, struct brcmf_netbuf* txp, bool success) {
  struct brcmf_bcdc* bcdc = static_cast<decltype(bcdc)>(drvr->proto->pd);
  struct brcmf_if* ifp;

  /* await txstatus signal for firmware if active */
  if (brcmf_fws_fc_active(bcdc->fws)) {
    if (!success) {
      brcmf_fws_bustxfail(bcdc->fws, txp);
    }
  } else {
    if (!brcmf_proto_bcdc_hdrpull(drvr, false, txp, &ifp)) {
      struct ethhdr* eh = (struct ethhdr*)(txp->data);
      brcmf_txfinalize(ifp, eh, success);
    }
    brcmu_pkt_buf_free_netbuf(txp);
  }
}

static void brcmf_proto_bcdc_configure_addr_mode(struct brcmf_pub* drvr, int ifidx,
                                                 enum proto_addr_mode addr_mode) {}

static void brcmf_proto_bcdc_delete_peer(struct brcmf_pub* drvr, int ifidx,
                                         uint8_t peer[ETH_ALEN]) {}

static void brcmf_proto_bcdc_add_tdls_peer(struct brcmf_pub* drvr, int ifidx,
                                           uint8_t peer[ETH_ALEN]) {}

static void brcmf_proto_bcdc_rxreorder(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {
  brcmf_fws_rxreorder(ifp, netbuf);
}

static void brcmf_proto_bcdc_add_if(struct brcmf_if* ifp) { brcmf_fws_add_interface(ifp); }

static void brcmf_proto_bcdc_del_if(struct brcmf_if* ifp) { brcmf_fws_del_interface(ifp); }

static void brcmf_proto_bcdc_reset_if(struct brcmf_if* ifp) { brcmf_fws_reset_interface(ifp); }

static zx_status_t brcmf_proto_bcdc_init_done(struct brcmf_pub* drvr) {
  struct brcmf_bcdc* bcdc = static_cast<decltype(bcdc)>(drvr->proto->pd);
  struct brcmf_fws_info* fws;
  zx_status_t err;

  err = brcmf_fws_attach(drvr, &fws);
  if (err != ZX_OK) {
    bcdc->fws = NULL;
    return err;
  }

  bcdc->fws = fws;
  return ZX_OK;
}

zx_status_t brcmf_proto_bcdc_attach(struct brcmf_pub* drvr) {
  struct brcmf_proto* proto = nullptr;
  struct brcmf_bcdc* bcdc = nullptr;

  proto = static_cast<decltype(proto)>(calloc(1, sizeof(*proto)));
  if (!proto) {
    goto fail;
  }

  bcdc = static_cast<decltype(bcdc)>(calloc(1, sizeof(*bcdc)));
  if (!bcdc) {
    goto fail;
  }

  /* ensure that the msg buf directly follows the cdc msg struct */
  if ((unsigned long)(&bcdc->msg + 1) != (unsigned long)bcdc->buf) {
    BRCMF_ERR("struct brcmf_proto_bcdc is not correctly defined");
    goto fail;
  }

  drvr->proto = proto;
  drvr->proto->hdrpull = brcmf_proto_bcdc_hdrpull;
  drvr->proto->query_dcmd = brcmf_proto_bcdc_query_dcmd;
  drvr->proto->set_dcmd = brcmf_proto_bcdc_set_dcmd;
  drvr->proto->tx_queue_data = brcmf_proto_bcdc_tx_queue_data;
  drvr->proto->txdata = brcmf_proto_bcdc_txdata;
  drvr->proto->configure_addr_mode = brcmf_proto_bcdc_configure_addr_mode;
  drvr->proto->delete_peer = brcmf_proto_bcdc_delete_peer;
  drvr->proto->add_tdls_peer = brcmf_proto_bcdc_add_tdls_peer;
  drvr->proto->rxreorder = brcmf_proto_bcdc_rxreorder;
  drvr->proto->add_if = brcmf_proto_bcdc_add_if;
  drvr->proto->del_if = brcmf_proto_bcdc_del_if;
  drvr->proto->reset_if = brcmf_proto_bcdc_reset_if;
  drvr->proto->init_done = brcmf_proto_bcdc_init_done;
  drvr->proto->pd = bcdc;

  drvr->hdrlen += BCDC_HEADER_LEN + BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES;
  drvr->bus_if->maxctl = BRCMF_DCMD_MAXLEN + sizeof(struct brcmf_proto_bcdc_dcmd);
  return ZX_OK;

fail:
  free(bcdc);
  free(proto);
  return ZX_ERR_NO_MEMORY;
}

void brcmf_proto_bcdc_detach(struct brcmf_pub* drvr) {
  if (drvr->proto == nullptr) {
    return;
  }

  struct brcmf_bcdc* bcdc = static_cast<decltype(bcdc)>(drvr->proto->pd);
  brcmf_fws_detach(bcdc->fws);

  drvr->proto->pd = nullptr;
  free(bcdc);
  free(drvr->proto);
  drvr->proto = nullptr;
}
