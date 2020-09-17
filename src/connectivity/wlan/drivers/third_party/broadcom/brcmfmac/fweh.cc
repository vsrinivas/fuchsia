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

#include "fweh.h"

#include <threads.h>
#include <zircon/status.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "cfg80211.h"
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "linuxisms.h"
#include "proto.h"
#include "workqueue.h"

// queue item type
enum brcmf_fweh_queue_item_type {
  BRCMF_FWEH_EVENT_INDICATION,
  BRCMF_FWEH_EAPOL_FRAME,
};
/**
 * struct brcmf_fweh_event_info
 * @code: event code.
 * @ifaddr: ethernet address for interface.
 * @emsg: common parameters of the firmware event message.
 */
struct brcmf_fweh_event_info {
  enum brcmf_fweh_event_code code;
  uint8_t ifaddr[ETH_ALEN];
  struct brcmf_event_msg_be emsg;
};

/**
 * struct brcmf_fweh_queue_item - event item on event queue.
 *
 * @q: list element for queuing.
 * @ifidx: interface index related to this event.
 * @item_type: of type brcmf_fweh_queue_item_type
 * @event_info of type brcmf_fweh_event_info.
 * @datalen: length of the data (below)
 * @data: event specific data part of the firmware event.ea
 */
struct brcmf_fweh_queue_item {
  struct list_node q;
  uint8_t ifidx;
  brcmf_fweh_queue_item_type item_type;
  brcmf_fweh_event_info event_info;
  uint32_t datalen;
  uint8_t data[0];
};

/**
 * struct brcmf_fweh_event_name - code, name mapping entry.
 */
struct brcmf_fweh_event_name {
  enum brcmf_fweh_event_code code;
  const char* name;
};

#define BRCMF_ENUM_DEF(id, val) {BRCMF_E_##id, #id},

/* array for mapping code to event name */
static struct brcmf_fweh_event_name fweh_event_names[] = {BRCMF_FWEH_EVENT_ENUM_DEFLIST};
#undef BRCMF_ENUM_DEF

/**
 * brcmf_fweh_event_name() - returns name for given event code.
 *
 * @code: code to lookup.
 */
const char* brcmf_fweh_event_name(enum brcmf_fweh_event_code code) {
  int i;
  for (i = 0; i < (int)countof(fweh_event_names); i++) {
    if (fweh_event_names[i].code == code) {
      return fweh_event_names[i].name;
    }
  }
  return "unknown";
}

/**
 * brcmf_fweh_queue_event() - create and queue event.
 * @drvr: driver context
 * @fweh: firmware event handling info.
 * @event: event queue entry.
 */
static void brcmf_fweh_queue_event(brcmf_pub* drvr, brcmf_fweh_info* fweh,
                                   brcmf_fweh_queue_item* event) {
  drvr->irq_callback_lock.lock();
  list_add_tail(&fweh->event_q, &event->q);
  drvr->irq_callback_lock.unlock();
  WorkQueue::ScheduleDefault(&fweh->event_work);
}

// Add eapol frame to the same queue as events (to ensure processing order).
void brcmf_fweh_queue_eapol_frame(struct brcmf_if* ifp, const void* data, size_t datalen) {
  brcmf_pub* drvr = ifp->drvr;
  // in SIM we do not use the queue - all events are processed inline.
  if (brcmf_bus_get_bus_type(drvr->bus_if) == BRCMF_BUS_TYPE_SIM) {
    brcmf_cfg80211_handle_eapol_frame(ifp, data, datalen);
    return;
  }

  struct brcmf_fweh_queue_item* event;
  // Allocate memory for the queue item. This gets released after processing in
  // brcmf_fweh_event_worker().
  event = static_cast<decltype(event)>(calloc(1, sizeof(*event) + datalen));
  if (!event) {
    BRCMF_ERR("calloc failed for eapol frame: %zu", datalen);
    return;
  }
  event->item_type = BRCMF_FWEH_EAPOL_FRAME;
  event->ifidx = ifp->ifidx;
  memcpy(event->data, data, datalen);
  event->datalen = datalen;
  // Queue the event item.
  brcmf_fweh_queue_event(drvr, &drvr->fweh, event);
}

static zx_status_t brcmf_fweh_call_event_handler(struct brcmf_if* ifp,
                                                 enum brcmf_fweh_event_code code,
                                                 struct brcmf_event_msg* emsg, void* data) {
  struct brcmf_fweh_info* fweh;
  zx_status_t err = ZX_ERR_IO;

  if (ifp) {
    fweh = &ifp->drvr->fweh;

    /* handle the event if valid interface and handler */
    if (fweh->evt_handler[code]) {
      err = fweh->evt_handler[code](ifp, emsg, data);
    } else {
      BRCMF_ERR("unhandled event %d ignored", code);
    }
  } else {
    BRCMF_ERR("no interface object");
  }
  return err;
}

/**
 * brcmf_fweh_handle_if_event() - handle IF event.
 *
 * @drvr: driver information object.
 * @item: queue entry.
 * @ifpp: interface object (may change upon ADD action).
 */
void brcmf_fweh_handle_if_event(struct brcmf_pub* drvr, struct brcmf_event_msg* emsg, void* data) {
  struct brcmf_if_event* ifevent = static_cast<decltype(ifevent)>(data);
  struct brcmf_if* ifp;
  zx_status_t err = ZX_OK;

  BRCMF_DBG(EVENT, "action: %u ifidx: %u bsscfgidx: %u flags: %u role: %u", ifevent->action,
            ifevent->ifidx, ifevent->bsscfgidx, ifevent->flags, ifevent->role);

  if (ifevent->flags & BRCMF_E_IF_FLAG_NOIF) {
    BRCMF_DBG(EVENT, "event can be ignored");
    return;
  }
  if (ifevent->ifidx >= BRCMF_MAX_IFS) {
    BRCMF_ERR("invalid interface index: %u", ifevent->ifidx);
    return;
  }

  ifp = drvr->iflist[ifevent->bsscfgidx];

  if (ifevent->action == BRCMF_E_IF_ADD) {
    BRCMF_DBG(EVENT, "adding ifname '%s' with mac %02x:%02x:%02x:%02x:%02x:%02x", emsg->ifname,
              emsg->addr[0], emsg->addr[1], emsg->addr[2], emsg->addr[3], emsg->addr[4],
              emsg->addr[5]);
    err = brcmf_add_if(drvr, ifevent->bsscfgidx, ifevent->ifidx, emsg->ifname, emsg->addr, &ifp);
    if (err != ZX_OK) {
      return;
    }
    brcmf_proto_add_if(drvr, ifp);

    if (!drvr->fweh.evt_handler[BRCMF_E_IF])
      if (brcmf_net_attach(ifp, false) != ZX_OK) {
        return;
      }
  }

  if (ifp && ifevent->action == BRCMF_E_IF_CHANGE) {
    brcmf_proto_reset_if(drvr, ifp);
  }

  err = brcmf_fweh_call_event_handler(ifp, static_cast<brcmf_fweh_event_code>(emsg->event_code),
                                      emsg, data);

  if (ifp && ifevent->action == BRCMF_E_IF_DEL) {
    bool armed = brcmf_cfg80211_vif_event_armed(drvr->config);

    /* Default handling in case no-one waits for this event */
    if (!armed) {
      brcmf_remove_interface(ifp, false);
    }
  }
}

/**
 * brcmf_fweh_handle_event() - call the handler for an event
 *
 * @drvr: driver information object.
 * @event_packet: event packet to handle.
 *
 * Converts the event message to host endianness and calls the
 * appropriate event handler, freeing the event upon completion.
 */
static void brcmf_fweh_handle_event(brcmf_pub* drvr, struct brcmf_fweh_queue_item* event) {
  struct brcmf_if* ifp;
  zx_status_t err = ZX_OK;
  struct brcmf_event_msg_be* emsg_be;
  struct brcmf_event_msg emsg;
  brcmf_fweh_event_info* event_info = &event->event_info;

  BRCMF_DBG(EVENT, "event %s (%u) ifidx %u bsscfg %u addr %pM",
            brcmf_fweh_event_name(event_info->code), event_info->code, event_info->emsg.ifidx,
            event_info->emsg.bsscfgidx, event_info->emsg.addr);

  /* convert event message */
  emsg_be = &event_info->emsg;
  emsg.version = be16toh(emsg_be->version);
  emsg.flags = be16toh(emsg_be->flags);
  emsg.event_code = event_info->code;
  emsg.status = be32toh(emsg_be->status);
  emsg.reason = be32toh(emsg_be->reason);
  emsg.auth_type = be32toh(emsg_be->auth_type);
  emsg.datalen = be32toh(emsg_be->datalen);
  memcpy(emsg.addr, emsg_be->addr, ETH_ALEN);
  memcpy(emsg.ifname, emsg_be->ifname, sizeof(emsg.ifname));
  emsg.ifidx = emsg_be->ifidx;
  emsg.bsscfgidx = emsg_be->bsscfgidx;

  BRCMF_DBG(EVENT, "  version %u flags %u status %u reason %u", emsg.version, emsg.flags,
            emsg.status, emsg.reason);
  BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(EVENT), event->data, std::min<uint32_t>(emsg.datalen, 64),
                     "event payload, len=%d", emsg.datalen);

  /* special handling of interface event */
  if (event_info->code == BRCMF_E_IF) {
    brcmf_fweh_handle_if_event(drvr, &emsg, event->data);
    goto event_free;
  }
  if (event_info->code == BRCMF_E_TDLS_PEER_EVENT) {
    ifp = drvr->iflist[0];
  } else {
    ifp = drvr->iflist[emsg.bsscfgidx];
  }

  err = brcmf_fweh_call_event_handler(ifp, event_info->code, &emsg, event->data);
  if (err != ZX_OK) {
    BRCMF_ERR("event handler failed (%d)", event_info->code);
    err = ZX_OK;
  }
event_free:
  free(event);
}

/**
 * brcmf_fweh_dequeue_event() - get event from the queue.
 *
 * @fweh: firmware event handling info.
 */
static struct brcmf_fweh_queue_item* brcmf_fweh_dequeue_event(brcmf_pub* drvr,
                                                              brcmf_fweh_info* fweh) {
  struct brcmf_fweh_queue_item* event = NULL;

  drvr->irq_callback_lock.lock();
  if (!list_is_empty(&fweh->event_q)) {
    event = list_peek_head_type(&fweh->event_q, struct brcmf_fweh_queue_item, q);
    list_delete(&event->q);
  }
  drvr->irq_callback_lock.unlock();

  return event;
}

/**
 * brcmf_fweh_event_worker() - firmware event worker.
 *
 * @work: worker object.
 */
static void brcmf_fweh_event_worker(WorkItem* work) {
  struct brcmf_fweh_queue_item* event;
  struct brcmf_fweh_info* fweh = containerof(work, struct brcmf_fweh_info, event_work);
  struct brcmf_pub* drvr = containerof(fweh, struct brcmf_pub, fweh);

  while ((event = brcmf_fweh_dequeue_event(drvr, fweh))) {
    if (event->item_type == BRCMF_FWEH_EVENT_INDICATION) {
      // This is an event.
      brcmf_fweh_handle_event(drvr, event);
    } else if (event->item_type == BRCMF_FWEH_EAPOL_FRAME) {
      // This is an eapol frame
      brcmf_if* ifp = brcmf_get_ifp(drvr, event->ifidx);
      if (ifp) {
        brcmf_cfg80211_handle_eapol_frame(ifp, event->data, event->datalen);
      }
      // Free the event item memory allocated in brcmf_fweh_queue_eapol_frame().
      free(event);
    }
  }
}

/**
 * brcmf_fweh_attach() - initialize firmware event handling.
 *
 * @drvr: driver information object.
 */
void brcmf_fweh_attach(struct brcmf_pub* drvr) {
  struct brcmf_fweh_info* fweh = &drvr->fweh;
  fweh->event_work = WorkItem(brcmf_fweh_event_worker);
  // spin_lock_init(&fweh->evt_q_lock);
  list_initialize(&fweh->event_q);
}

/**
 * brcmf_fweh_detach() - cleanup firmware event handling.
 *
 * @drvr: driver information object.
 */
void brcmf_fweh_detach(struct brcmf_pub* drvr) {
  struct brcmf_fweh_info* fweh = &drvr->fweh;
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, 0);
  int8_t eventmask[BRCMF_EVENTING_MASK_LEN];

  if (ifp) {
    /* clear all events */
    memset(eventmask, 0, BRCMF_EVENTING_MASK_LEN);
    (void)brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN, nullptr);
  }
  /* cancel the worker */
  fweh->event_work.Cancel();
  WARN_ON(!list_is_empty(&fweh->event_q));
  memset(fweh->evt_handler, 0, sizeof(fweh->evt_handler));
}

/**
 * brcmf_fweh_register() - register handler for given event code.
 *
 * @drvr: driver information object.
 * @code: event code.
 * @handler: handler for the given event code.
 */
zx_status_t brcmf_fweh_register(struct brcmf_pub* drvr, enum brcmf_fweh_event_code code,
                                brcmf_fweh_handler_t handler) {
  if (drvr->fweh.evt_handler[code]) {
    BRCMF_ERR("Exit: event code %d already registered", code);
    return ZX_ERR_ALREADY_EXISTS;
  }
  drvr->fweh.evt_handler[code] = handler;
  BRCMF_DBG(TRACE, "Exit: event handler registered for %s", brcmf_fweh_event_name(code));
  return ZX_OK;
}

/**
 * brcmf_fweh_unregister() - remove handler for given code.
 *
 * @drvr: driver information object.
 * @code: event code.
 */
void brcmf_fweh_unregister(struct brcmf_pub* drvr, enum brcmf_fweh_event_code code) {
  BRCMF_DBG(TRACE, "event handler cleared for %s", brcmf_fweh_event_name(code));
  drvr->fweh.evt_handler[code] = NULL;
}

/**
 * brcmf_fweh_activate_events() - enables firmware events registered.
 *
 * @ifp: primary interface object.
 */
zx_status_t brcmf_fweh_activate_events(struct brcmf_if* ifp) {
  int i;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;
  struct brcmf_eventmsgs_ext eventmsgs_ext;
  size_t msg_len = BRCMF_EVENTING_MASK_LEN + EVENTMSGS_EXT_STRUCT_SIZE;
  bool support_eventmsgs_ext = true;

  // Initialize the struct with 0.
  memset(&eventmsgs_ext, 0, msg_len);

  err = brcmf_fil_iovar_data_get(ifp, "event_msgs_ext", &eventmsgs_ext, msg_len, &fw_err);
  if (err != ZX_OK) {
    // Use original way to read event mask from firmware if eventmsgs_ext is not supported by
    // firmware.
    if (fw_err == BCME_VERSION) {
      support_eventmsgs_ext = false;
      BRCMF_WARN("event_msgs_ext is not supported in current firmware");
      err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmsgs_ext.event_mask,
                                     BRCMF_EVENTING_MASK_LEN, &fw_err);
      if (err != ZX_OK) {
        BRCMF_ERR("Set event_msgs(_ext) error: %s, fw err %s", zx_status_get_string(err),
                  brcmf_fil_get_errstr(fw_err));
        return err;
      }
    } else {
      BRCMF_ERR("Get event_msgs_ext error: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
      return err;
    }
  }

  for (i = 0; i < BRCMF_E_LAST; i++) {
    if (ifp->drvr->fweh.evt_handler[i]) {
      BRCMF_DBG(EVENT, "enable event %s",
                brcmf_fweh_event_name(static_cast<brcmf_fweh_event_code>(i)));
      setbit(eventmsgs_ext.event_mask, i);
    }
  }

  /* want to handle IF event as well */
  BRCMF_DBG(EVENT, "enable event IF");
  setbit(eventmsgs_ext.event_mask, BRCMF_E_IF);

  if (!support_eventmsgs_ext) {
    // Use original way to write event mask from firmware if eventmsgs_ext is not supported by
    // firmware.
    err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmsgs_ext.event_mask,
                                   BRCMF_EVENTING_MASK_LEN, &fw_err);
  } else {
    // Use "event_msgs_ext" to write extended event mask to firmware.
    eventmsgs_ext.version = EVENTMSGS_VERSION;
    eventmsgs_ext.command = EVENTMSGS_SET_MASK;
    eventmsgs_ext.length = BRCMF_EVENTING_MASK_LEN;

    err = brcmf_fil_iovar_data_set(ifp, "event_msgs_ext", &eventmsgs_ext, msg_len, &fw_err);
  }
  if (err != ZX_OK) {
    BRCMF_ERR("Set event_msgs(_ext) error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  return err;
}

/**
 * brcmf_fweh_process_event() - process firmware event.
 *
 * @drvr: driver information object.
 * @event_packet: event packet to process.
 *
 * If the packet buffer contains a firmware event message it will
 * dispatch the event to a registered handler (using worker).
 */
void brcmf_fweh_process_event(struct brcmf_pub* drvr, const struct brcmf_event* event_packet,
                              uint32_t packet_len) {
  enum brcmf_fweh_event_code code;
  struct brcmf_fweh_info* fweh = &drvr->fweh;
  struct brcmf_fweh_queue_item* event;
  brcmf_fweh_event_info* event_info;
  uint16_t usr_stype;
  const void* data;
  uint32_t datalen;

  if (packet_len < sizeof(*event_packet)) {
    return;
  }

  /* only process events when protocol matches */
  if (event_packet->eth.h_proto != htobe16(ETH_P_LINK_CTL)) {
    return;
  }

  /* check for BRCM oui match */
  if (memcmp(BRCM_OUI, &event_packet->hdr.oui[0], sizeof(event_packet->hdr.oui))) {
    return;
  }

  /* final match on usr_subtype */
  usr_stype = be16toh(event_packet->hdr.usr_subtype);
  if (usr_stype != BCMILCP_BCM_SUBTYPE_EVENT) {
    return;
  }

  /* get event info */
  code = static_cast<brcmf_fweh_event_code>(be32toh(event_packet->msg.event_type));
  datalen = be32toh(event_packet->msg.datalen);
  data = &event_packet[1];

  if (code >= BRCMF_E_LAST) {
    return;
  }

  if (code != BRCMF_E_IF && !fweh->evt_handler[code]) {
    BRCMF_DBG(TEMP, "Event not found");
    return;
  }

  if (datalen > BRCMF_DCMD_MAXLEN || datalen + sizeof(*event_packet) > packet_len) {
    BRCMF_DBG(TEMP, "Len, datalen %d, event_packet size %ld, packet_len %d", datalen,
              sizeof(*event_packet), packet_len);
    return;
  }

  event = static_cast<decltype(event)>(calloc(1, sizeof(*event) + datalen));
  if (!event) {
    return;
  }

  event_info = &event->event_info;
  event->item_type = BRCMF_FWEH_EVENT_INDICATION;
  event_info->code = code;
  event->ifidx = event_packet->msg.ifidx;

  /* use memcpy to get aligned event message */
  memcpy(&event_info->emsg, &event_packet->msg, sizeof(event_info->emsg));
  memcpy(event->data, data, datalen);
  event->datalen = datalen;
  memcpy(event_info->ifaddr, event_packet->eth.h_dest, ETH_ALEN);

  if (brcmf_bus_get_bus_type(drvr->bus_if) == BRCMF_BUS_TYPE_SIM) {
    // The simulator's behavior is synchronous: we want all events to be processed immediately.
    // So, we bypass the workqueue and just call directly into the handler.
    brcmf_fweh_handle_event(drvr, event);
  } else {
    brcmf_fweh_queue_event(drvr, fweh, event);
  }
}
