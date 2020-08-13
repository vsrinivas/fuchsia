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

/* FWIL is the Firmware Interface Layer. In this module the support functions
 * are located to set and get variables to and from the firmware.
 */

#include "fwil.h"

#include <threads.h>
#include <zircon/status.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "core.h"
#include "debug.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "proto.h"

#define MAX_HEX_DUMP_LEN 64

#define F(BCME_STATUS) X(BCME_STATUS)
#define X(BCME_STATUS) \
  case BCME_STATUS:    \
    return "BCME_STATUS";
const char* brcmf_fil_get_errstr(bcme_status_t err) {
  switch (err) { BCME_STATUS_LIST };
  return "(unknown)";
}
#undef X
#undef F

static zx_status_t brcmf_fil_cmd_data(struct brcmf_if* ifp, uint32_t cmd, void* data, uint32_t len,
                                      bool set, bcme_status_t* fwerr_ptr) {
  struct brcmf_pub* drvr = ifp->drvr;
  zx_status_t err;
  bcme_status_t fwerr = BCME_OK;

  if (drvr->bus_if->state != BRCMF_BUS_UP) {
    BRCMF_ERR("bus is down. we have nothing to do.");
    return ZX_ERR_IO;
  }

  if (data != NULL) {
    len = std::min<uint>(len, BRCMF_DCMD_MAXLEN);
  }
  if (set) {
    err = brcmf_proto_set_dcmd(drvr, ifp->ifidx, cmd, data, len, &fwerr);
  } else {
    err = brcmf_proto_query_dcmd(drvr, ifp->ifidx, cmd, data, len, &fwerr);
  }

  if (err != ZX_OK) {
    BRCMF_DBG(FIL, "Failed: %s", zx_status_get_string(err));
  } else if (fwerr != 0) {
    BRCMF_DBG(FIL, "Firmware error: %s (%d)", brcmf_fil_get_errstr(fwerr), fwerr);
    if (fwerr == BCME_UNSUPPORTED) {
      err = ZX_ERR_NOT_SUPPORTED;
    } else {
      err = ZX_ERR_IO_REFUSED;
    }
  }

  if (fwerr_ptr != nullptr) {
    *fwerr_ptr = fwerr;
  }

  return err;
}

zx_status_t brcmf_fil_cmd_data_set(struct brcmf_if* ifp, uint32_t cmd, const void* data,
                                   uint32_t len, bcme_status_t* fwerr_ptr) {
  zx_status_t err;

  ifp->drvr->proto_block.lock();

  BRCMF_DBG(FIL, "ifidx=%d, cmd=%d, len=%d", ifp->ifidx, cmd, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  err = brcmf_fil_cmd_data(ifp, cmd, (void*)data, len, true, fwerr_ptr);
  ifp->drvr->proto_block.unlock();

  return err;
}

zx_status_t brcmf_fil_cmd_data_get(struct brcmf_if* ifp, uint32_t cmd, void* data, uint32_t len,
                                   bcme_status_t* fwerr_ptr) {
  zx_status_t err;

  ifp->drvr->proto_block.lock();
  err = brcmf_fil_cmd_data(ifp, cmd, data, len, false, fwerr_ptr);

  BRCMF_DBG(FIL, "ifidx=%d, cmd=%d, len=%d", ifp->ifidx, cmd, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  ifp->drvr->proto_block.unlock();

  return err;
}

zx_status_t brcmf_fil_cmd_int_set(struct brcmf_if* ifp, uint32_t cmd, uint32_t data,
                                  bcme_status_t* fwerr_ptr) {
  zx_status_t err;
  uint32_t data_le = data;

  ifp->drvr->proto_block.lock();
  BRCMF_DBG(FIL, "ifidx=%d, cmd=%d, value=%d", ifp->ifidx, cmd, data);
  err = brcmf_fil_cmd_data(ifp, cmd, &data_le, sizeof(data_le), true, fwerr_ptr);
  ifp->drvr->proto_block.unlock();

  return err;
}

zx_status_t brcmf_fil_cmd_int_get(struct brcmf_if* ifp, uint32_t cmd, uint32_t* data,
                                  bcme_status_t* fwerr_ptr) {
  zx_status_t err;
  uint32_t data_le = *data;

  ifp->drvr->proto_block.lock();
  err = brcmf_fil_cmd_data(ifp, cmd, &data_le, sizeof(data_le), false, fwerr_ptr);
  ifp->drvr->proto_block.unlock();
  *data = data_le;
  BRCMF_DBG(FIL, "ifidx=%d, cmd=%d, value=%d", ifp->ifidx, cmd, *data);

  return err;
}

static uint32_t brcmf_create_iovar(const char* name, const void* data, uint32_t datalen, char* buf,
                                   uint32_t buflen) {
  uint32_t len;

  len = strlen(name) + 1;

  if ((len + datalen) > buflen) {
    return 0;
  }

  memcpy(buf, name, len);

  /* append data onto the end of the name string */
  if (data && datalen) {
    memcpy(&buf[len], data, datalen);
  }

  return len + datalen;
}

zx_status_t brcmf_fil_iovar_data_set(struct brcmf_if* ifp, const char* name, const void* data,
                                     uint32_t len, bcme_status_t* fwerr_ptr) {
  struct brcmf_pub* drvr = ifp->drvr;
  zx_status_t err;
  bcme_status_t fwerr;
  uint32_t buflen;

  drvr->proto_block.lock();
  BRCMF_DBG(FIL, "ifidx=%d, name=%s, len=%d", ifp->ifidx, name, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  buflen = brcmf_create_iovar(name, data, len, (char*)drvr->proto_buf, sizeof(drvr->proto_buf));
  if (buflen) {
    err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf, buflen, true, &fwerr);
    if (fwerr_ptr) {
      *fwerr_ptr = fwerr;
    }

    if (err != ZX_OK) {
      BRCMF_DBG(FIL, "Failed to set iovar %s: %s, fw err %s", name, zx_status_get_string(err),
                brcmf_fil_get_errstr(fwerr));
    }
  } else {
    err = ZX_ERR_BUFFER_TOO_SMALL;
    BRCMF_ERR("Failed to create iovar %s: %s", name, zx_status_get_string(err));
  }

  drvr->proto_block.unlock();
  return err;
}

zx_status_t brcmf_fil_iovar_data_get(struct brcmf_if* ifp, const char* name, void* data,
                                     uint32_t len, bcme_status_t* fwerr_ptr) {
  struct brcmf_pub* drvr = ifp->drvr;
  zx_status_t err;
  bcme_status_t fwerr;
  uint32_t buflen;
  drvr->proto_block.lock();
  buflen = brcmf_create_iovar(name, data, len, (char*)drvr->proto_buf, sizeof(drvr->proto_buf));

  if (buflen) {
    err = brcmf_fil_cmd_data(ifp, BRCMF_C_GET_VAR, drvr->proto_buf, buflen, false, &fwerr);
    if (fwerr_ptr) {
      *fwerr_ptr = fwerr;
    }

    if (err == ZX_OK) {
      memcpy(data, drvr->proto_buf, len);
    } else {
      BRCMF_DBG(FIL, "Failed to get iovar %s: %s, fw err %s", name, zx_status_get_string(err),
                brcmf_fil_get_errstr(fwerr));
    }
  } else {
    err = ZX_ERR_BUFFER_TOO_SMALL;
    BRCMF_ERR("Failed to create iovar %s: %s", name, zx_status_get_string(err));
  }

  BRCMF_DBG(FIL, "ifidx=%d, name=%s, len=%d", ifp->ifidx, name, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  drvr->proto_block.unlock();
  return err;
}

zx_status_t brcmf_fil_iovar_int_set(struct brcmf_if* ifp, const char* name, uint32_t data,
                                    bcme_status_t* fwerr_ptr) {
  uint32_t data_le = data;

  return brcmf_fil_iovar_data_set(ifp, name, &data_le, sizeof(data_le), fwerr_ptr);
}

zx_status_t brcmf_fil_iovar_int_get(struct brcmf_if* ifp, const char* name, uint32_t* data,
                                    bcme_status_t* fwerr_ptr) {
  uint32_t data_le = *data;
  zx_status_t err;

  err = brcmf_fil_iovar_data_get(ifp, name, &data_le, sizeof(data_le), fwerr_ptr);
  if (err == ZX_OK) {
    *data = data_le;
  }
  return err;
}

static uint32_t brcmf_create_bsscfg(int32_t bsscfgidx, const char* name, const void* data,
                                    uint32_t datalen, char* buf, uint32_t buflen) {
  const char* prefix = BRCMF_FWIL_BSSCFG_PREFIX;
  char* p;
  uint32_t prefixlen;
  uint32_t namelen;
  uint32_t iolen;
  uint32_t bsscfgidx_le;

  if (bsscfgidx == 0) {
    return brcmf_create_iovar(name, data, datalen, buf, buflen);
  }

  prefixlen = strlen(prefix);
  namelen = strlen(name) + 1; /* lengh of iovar  name + null */
  iolen = prefixlen + namelen + sizeof(bsscfgidx_le) + datalen;

  if (buflen < iolen) {
    BRCMF_ERR("buffer is too short");
    return 0;
  }

  p = buf;

  /* copy prefix, no null */
  memcpy(p, prefix, prefixlen);
  p += prefixlen;

  /* copy iovar name including null */
  memcpy(p, name, namelen);
  p += namelen;

  /* bss config index as first data */
  bsscfgidx_le = bsscfgidx;
  memcpy(p, &bsscfgidx_le, sizeof(bsscfgidx_le));
  p += sizeof(bsscfgidx_le);

  /* parameter buffer follows */
  if (datalen) {
    memcpy(p, data, datalen);
  }

  return iolen;
}

zx_status_t brcmf_fil_bsscfg_data_set(struct brcmf_if* ifp, const char* name, const void* data,
                                      uint32_t len) {
  struct brcmf_pub* drvr = ifp->drvr;
  zx_status_t err;
  uint32_t buflen;

  drvr->proto_block.lock();

  BRCMF_DBG(FIL, "ifidx=%d, bsscfgidx=%d, name=%s, len=%d", ifp->ifidx, ifp->bsscfgidx, name, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  buflen = brcmf_create_bsscfg(ifp->bsscfgidx, name, data, len, (char*)drvr->proto_buf,
                               sizeof(drvr->proto_buf));
  if (buflen) {
    err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf, buflen, true, nullptr);
  } else {
    err = ZX_ERR_BUFFER_TOO_SMALL;
    BRCMF_ERR("Creating bsscfg failed");
  }

  drvr->proto_block.unlock();
  return err;
}

zx_status_t brcmf_fil_bsscfg_data_get(struct brcmf_if* ifp, const char* name, void* data,
                                      uint32_t len) {
  struct brcmf_pub* drvr = ifp->drvr;
  zx_status_t err;
  uint32_t buflen;

  drvr->proto_block.lock();

  buflen = brcmf_create_bsscfg(ifp->bsscfgidx, name, data, len, (char*)drvr->proto_buf,
                               sizeof(drvr->proto_buf));
  if (buflen) {
    err = brcmf_fil_cmd_data(ifp, BRCMF_C_GET_VAR, drvr->proto_buf, buflen, false, nullptr);
    if (err == ZX_OK) {
      memcpy(data, drvr->proto_buf, len);
    }
  } else {
    err = ZX_ERR_BUFFER_TOO_SMALL;
    BRCMF_ERR("Creating bsscfg failed");
  }
  BRCMF_DBG(FIL, "ifidx=%d, bsscfgidx=%d, name=%s, len=%d", ifp->ifidx, ifp->bsscfgidx, name, len);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(FIL), data, std::min<uint>(len, MAX_HEX_DUMP_LEN), "data");

  drvr->proto_block.unlock();
  return err;
}

zx_status_t brcmf_fil_bsscfg_int_set(struct brcmf_if* ifp, const char* name, uint32_t data) {
  uint32_t data_le = data;

  return brcmf_fil_bsscfg_data_set(ifp, name, &data_le, sizeof(data_le));
}

zx_status_t brcmf_fil_bsscfg_int_get(struct brcmf_if* ifp, const char* name, uint32_t* data) {
  uint32_t data_le = *data;
  zx_status_t err;

  err = brcmf_fil_bsscfg_data_get(ifp, name, &data_le, sizeof(data_le));
  if (err == ZX_OK) {
    *data = data_le;
  }
  return err;
}

// Send iovar command to firmware and return status
zx_status_t brcmf_send_cmd_to_firmware(brcmf_pub* drvr, uint32_t ifidx, uint32_t cmd, void* data,
                                       uint32_t len, bool set) {
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, ifidx);

  if (!ifp)
    return ZX_ERR_UNAVAILABLE;

  return brcmf_fil_cmd_data(ifp, cmd, data, len, set, nullptr);
}
