// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/arch_ops.h>

#include "dwc3.h"
#include "dwc3-regs.h"

#include <fbl/auto_lock.h>
#include <stdio.h>
#include <unistd.h>

static void dwc3_handle_ep_event(dwc3_t* dwc, uint32_t event) {
  uint32_t type = DEPEVT_TYPE(event);
  uint32_t ep_num = DEPEVT_PHYS_EP(event);
  uint32_t status = DEPEVT_STATUS(event);

  switch (type) {
    case DEPEVT_XFER_COMPLETE:
      dwc3_ep_xfer_complete(dwc, ep_num);
      break;
    case DEPEVT_XFER_IN_PROGRESS:
      zxlogf(TRACE, "DEPEVT_XFER_IN_PROGRESS ep_num: %u status %u\n", ep_num, status);
      break;
    case DEPEVT_XFER_NOT_READY:
      dwc3_ep_xfer_not_ready(dwc, ep_num, DEPEVT_XFER_NOT_READY_STAGE(event));
      break;
    case DEPEVT_STREAM_EVT:
      zxlogf(TRACE, "DEPEVT_STREAM_EVT ep_num: %u status %u\n", ep_num, status);
      break;
    case DEPEVT_CMD_CMPLT: {
      unsigned cmd_type = DEPEVT_CMD_CMPLT_CMD_TYPE(event);
      unsigned rsrc_id = DEPEVT_CMD_CMPLT_RSRC_ID(event);
      if (cmd_type == DEPCMD::DEPSTRTXFER) {
        dwc3_ep_xfer_started(dwc, ep_num, rsrc_id);
      }
      break;
    }
    default:
      zxlogf(ERROR, "dwc3_handle_ep_event: unknown event type %u\n", type);
      break;
  }
}

static void dwc3_handle_event(dwc3_t* dwc, uint32_t event) {
  zxlogf(LTRACE, "dwc3_handle_event %08X\n", event);
  if (!(event & DEPEVT_NON_EP)) {
    dwc3_handle_ep_event(dwc, event);
    return;
  }

  uint32_t type = DEVT_TYPE(event);
  uint32_t info = DEVT_INFO(event);

  switch (type) {
    case DEVT_DISCONNECT:
      zxlogf(TRACE, "DEVT_DISCONNECT\n");
      break;
    case DEVT_USB_RESET:
      zxlogf(TRACE, "DEVT_USB_RESET\n");
      dwc3_usb_reset(dwc);
      break;
    case DEVT_CONNECTION_DONE:
      zxlogf(TRACE, "DEVT_CONNECTION_DONE\n");
      dwc3_connection_done(dwc);
      break;
    case DEVT_LINK_STATE_CHANGE:
      zxlogf(TRACE, "DEVT_LINK_STATE_CHANGE: ");
      switch (info) {
        case DSTS::USBLNKST_U0 | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS::USBLNKST_U0\n");
          break;
        case DSTS::USBLNKST_U1 | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_U1\n");
          break;
        case DSTS::USBLNKST_U2 | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_U2\n");
          break;
        case DSTS::USBLNKST_U3 | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_U3\n");
          break;
        case DSTS::USBLNKST_ESS_DIS | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_ESS_DIS\n");
          break;
        case DSTS::USBLNKST_RX_DET | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_RX_DET\n");
          break;
        case DSTS::USBLNKST_ESS_INACT | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_ESS_INACT\n");
          break;
        case DSTS::USBLNKST_POLL | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_POLL\n");
          break;
        case DSTS::USBLNKST_RECOV | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_RECOV\n");
          break;
        case DSTS::USBLNKST_HRESET | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_HRESET\n");
          break;
        case DSTS::USBLNKST_CMPLY | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_CMPLY\n");
          break;
        case DSTS::USBLNKST_LPBK | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_LPBK\n");
          break;
        case DSTS::USBLNKST_RESUME_RESET | DEVT_LINK_STATE_CHANGE_SS:
          zxlogf(TRACE, "DSTS_USBLNKST_RESUME_RESET\n");
          break;
        case DSTS::USBLNKST_ON:
          zxlogf(TRACE, "DSTS_USBLNKST_ON\n");
          break;
        case DSTS::USBLNKST_SLEEP:
          zxlogf(TRACE, "DSTS_USBLNKST_SLEEP\n");
          break;
        case DSTS::USBLNKST_SUSPEND:
          zxlogf(TRACE, "DSTS_USBLNKST_SUSPEND\n");
          break;
        case DSTS::USBLNKST_DISCONNECTED:
          zxlogf(TRACE, "DSTS_USBLNKST_DISCONNECTED\n");
          break;
        case DSTS::USBLNKST_EARLY_SUSPEND:
          zxlogf(TRACE, "DSTS_USBLNKST_EARLY_SUSPEND\n");
          break;
        case DSTS::USBLNKST_RESET:
          zxlogf(TRACE, "DSTS_USBLNKST_RESET\n");
          break;
        case DSTS::USBLNKST_RESUME:
          zxlogf(TRACE, "DSTS_USBLNKST_RESUME\n");
          break;
        default:
          zxlogf(ERROR, "unknown state %d\n", info);
          break;
      }
      break;
    case DEVT_REMOTE_WAKEUP:
      zxlogf(TRACE, "DEVT_REMOTE_WAKEUP\n");
      break;
    case DEVT_HIBERNATE_REQUEST:
      zxlogf(TRACE, "DEVT_HIBERNATE_REQUEST\n");
      break;
    case DEVT_SUSPEND_ENTRY:
      zxlogf(TRACE, "DEVT_SUSPEND_ENTRY\n");
      // TODO(voydanoff) is this the best way to detect disconnect?
      dwc3_disconnected(dwc);
      break;
    case DEVT_SOF:
      zxlogf(TRACE, "DEVT_SOF\n");
      break;
    case DEVT_ERRATIC_ERROR:
      zxlogf(TRACE, "DEVT_ERRATIC_ERROR\n");
      break;
    case DEVT_COMMAND_COMPLETE:
      zxlogf(TRACE, "DEVT_COMMAND_COMPLETE\n");
      break;
    case DEVT_EVENT_BUF_OVERFLOW:
      zxlogf(TRACE, "DEVT_EVENT_BUF_OVERFLOW\n");
      break;
    case DEVT_VENDOR_TEST_LMP:
      zxlogf(TRACE, "DEVT_VENDOR_TEST_LMP\n");
      break;
    case DEVT_STOPPED_DISCONNECT:
      zxlogf(TRACE, "DEVT_STOPPED_DISCONNECT\n");
      break;
    case DEVT_L1_RESUME_DETECT:
      zxlogf(TRACE, "DEVT_L1_RESUME_DETECT\n");
      break;
    case DEVT_LDM_RESPONSE:
      zxlogf(TRACE, "DEVT_LDM_RESPONSE\n");
      break;
    default:
      zxlogf(ERROR, "dwc3_handle_event: unknown event type %u\n", type);
      break;
  }
}

static int dwc3_irq_thread(void* arg) {
  auto* dwc = static_cast<dwc3_t*>(arg);
  auto* mmio = dwc3_mmio(dwc);

  zxlogf(TRACE, "dwc3_irq_thread start\n");

  auto* ring_start = static_cast<uint32_t*>(io_buffer_virt(&dwc->event_buffer));
  auto* ring_end = ring_start + EVENT_BUFFER_SIZE / sizeof(*ring_start);
  auto* ring_cur = ring_start;
  while (1) {
    {
      fbl::AutoLock l(&dwc->pending_completions_lock);
      if (!list_is_empty(&dwc->pending_completions)) {
        dwc_usb_req_internal_t* req_int;
        list_node_t list;
        list_move(&dwc->pending_completions, &list);
        l.release();
        while ((req_int = list_remove_head_type(&list, dwc_usb_req_internal_t, pending_node)) !=
               nullptr) {
          req_int->complete_cb.callback(req_int->complete_cb.ctx, INTERNAL_TO_USB_REQ(req_int));
        }
      }
    }
    zx_status_t status = dwc->irq_handle.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "dwc3_irq_thread: zx_interrupt_wait returned %d\n", status);
      break;
    }
    // read number of new bytes in the event buffer
    uint32_t event_count;
    while ((event_count = GEVNTCOUNT::Get(0).ReadFrom(mmio).EVNTCOUNT()) > 0) {
      // invalidate cache so we can read fresh events
      io_buffer_cache_flush_invalidate(&dwc->event_buffer, 0, EVENT_BUFFER_SIZE);

      for (uint32_t i = 0; i < event_count; i += static_cast<uint32_t>(sizeof(uint32_t))) {
        uint32_t event = *ring_cur++;
        if (ring_cur == ring_end) {
          ring_cur = ring_start;
        }
        dwc3_handle_event(dwc, event);
      }

      // acknowledge the events we have processed
      GEVNTCOUNT::Get(0).FromValue(0).set_EVNTCOUNT(event_count).WriteTo(mmio);
    }
  }

  zxlogf(TRACE, "dwc3_irq_thread done\n");
  return 0;
}

void dwc3_events_start(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  // set event buffer pointer and size
  // keep interrupts masked until we are ready
  zx_paddr_t paddr = io_buffer_phys(&dwc->event_buffer);
  GEVNTADR::Get(0).FromValue(0).set_EVNTADR(paddr).WriteTo(mmio);
  GEVNTSIZ::Get(0).FromValue(0).set_EVENTSIZ(EVENT_BUFFER_SIZE).set_EVNTINTRPTMASK(1).WriteTo(mmio);
  GEVNTCOUNT::Get(0).FromValue(0).set_EVNTCOUNT(0).WriteTo(mmio);

  // enable events
  DEVTEN::Get()
      .FromValue(0)
      .set_L1SUSPEN(1)
      .set_U3L2L1SuspEn(1)
      .set_CONNECTDONEEVTEN(1)
      .set_USBRSTEVTEN(1)
      .set_DISSCONNEVTEN(1)
      .WriteTo(mmio);

  thrd_create_with_name(&dwc->irq_thread, dwc3_irq_thread, dwc, "dwc3_irq_thread");
}

void dwc3_events_stop(dwc3_t* dwc) {
  dwc->irq_handle.destroy();
  thrd_join(dwc->irq_thread, nullptr);
}
