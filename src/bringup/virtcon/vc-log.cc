// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include "vc.h"

static zx_koid_t proc_koid;
static port_handler_t log_ph;
static vc_t* log_vc;

// This is the list for logs on displays other than the main display.
static struct list_node log_list = LIST_INITIAL_VALUE(log_list);

void set_log_listener_active(bool active) {
  if (active) {
    port_wait(&port, &log_ph);
  } else {
    port_cancel(&port, &log_ph);
  }
}

zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out) {
  vc_t* vc;
  zx_status_t status = vc_alloc(&vc, &color_schemes[kDefaultColorScheme]);
  if (status != ZX_OK) {
    return status;
  }

  // Copy the log buffer into the new vc.
  size_t textbuf_size = log_vc->rows * log_vc->columns * sizeof(vc_char_t);
  memcpy(vc->text_buf, log_vc->text_buf, textbuf_size);
  vc->cursor_x = log_vc->cursor_x;
  vc->cursor_y = log_vc->cursor_y;

  // Set the new vc graphics and flush the text.
  vc->active = true;
  vc->graphics = graphics;
  vc_attach_gfx(vc);
  vc_full_repaint(vc);

  list_add_tail(&log_list, &vc->node);
  *vc_out = vc;
  return ZX_OK;
}

void log_delete_vc(vc_t* vc) {
  list_delete(&vc->node);
  vc_free(vc);
}

int log_start(void) {
  // Create initial console for debug log.
  if (vc_create(&log_vc, &color_schemes[kDefaultColorScheme]) != ZX_OK) {
    return -1;
  }
  snprintf(log_vc->title, sizeof(log_vc->title), "debuglog");

  // Get our process koid so the log reader can
  // filter out our own debug messages from the log.
  zx_info_handle_basic_t info;
  if (zx_object_get_info(zx_process_self(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL,
                         NULL) == ZX_OK) {
    proc_koid = info.koid;
  }

  log_ph.handle = zx_take_startup_handle(PA_HND(PA_USER0, 1));
  if (log_ph.handle == ZX_HANDLE_INVALID) {
    printf("vc log listener: did not receive log startup handle\n");
    return -1;
  }

  log_ph.func = log_reader_cb;
  log_ph.waitfor = ZX_LOG_READABLE;

  return 0;
}

static void write_to_log(vc_t* vc, zx_log_record_t* rec) {
  char tmp[64];
  snprintf(tmp, 64,
           "\033[32m%05d.%03d\033[39m] \033[31m%05" PRIu64 ".\033[36m%05" PRIu64 "\033[39m> ",
           (int)(rec->timestamp / 1000000000ULL), (int)((rec->timestamp / 1000000ULL) % 1000ULL),
           rec->pid, rec->tid);
  vc_write(vc, tmp, strlen(tmp), 0);
  vc_write(vc, rec->data, rec->datalen, 0);
  if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
    vc_write(vc, "\n", 1, 0);
  }
}

zx_status_t log_reader_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  char buf[ZX_LOG_RECORD_MAX];
  zx_log_record_t* rec = (zx_log_record_t*)buf;
  zx_status_t status;
  for (;;) {
    if ((status = zx_debuglog_read(ph->handle, 0, rec, ZX_LOG_RECORD_MAX)) < 0) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        return ZX_OK;
      }
      break;
    }
    // Don't print log messages from ourself.
    if (rec->pid == proc_koid) {
      continue;
    }
    write_to_log(log_vc, rec);

    vc_t* vc = NULL;
    list_for_every_entry (&log_list, vc, vc_t, node) { write_to_log(vc, rec); }
  }

  const char* oops = "<<LOG ERROR>>\n";
  vc_write(log_vc, oops, strlen(oops), 0);

  // Error reading the log, no point in continuing to try to read
  // log messages.
  port_cancel(&port, &log_ph);
  return status;
}
