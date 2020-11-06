// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <optional>

#include "vc.h"

vc_t* g_log_vc;

static zx_koid_t proc_koid;
static std::optional<async::Wait> log_wait;
static async_dispatcher_t* log_dispatcher = nullptr;

static void log_reader_cb(async_dispatcher_t* dispatcher, async::Wait* wait,
                          zx_status_t wait_status, const zx_packet_signal_t* signal);

// This is the list for logs on displays other than the main display.
static struct list_node log_list = LIST_INITIAL_VALUE(log_list);

void set_log_listener_active(bool active) {
  if (active) {
    log_wait->Begin(log_dispatcher);
  } else {
    log_wait->Cancel();
  }
}

zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out) {
  vc_t* vc;
  zx_status_t status = vc_alloc(&vc, &color_schemes[kDefaultColorScheme]);
  if (status != ZX_OK) {
    return status;
  }

  // Copy the log buffer into the new vc.
  size_t textbuf_size = g_log_vc->rows * g_log_vc->columns * sizeof(*vc->text_buf);
  memcpy(vc->text_buf, g_log_vc->text_buf, textbuf_size);
  vc->cursor_x = g_log_vc->cursor_x;
  vc->cursor_y = g_log_vc->cursor_y;

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

int log_start(async_dispatcher_t* dispatcher, zx::debuglog read_only_debuglog,
              const color_scheme_t* color_scheme) {
  log_dispatcher = dispatcher;
  // Create initial console for debug log.
  if (vc_create(&g_log_vc, color_scheme) != ZX_OK) {
    return -1;
  }
  snprintf(g_log_vc->title, sizeof(g_log_vc->title), "debuglog");

  // Get our process koid so the log reader can
  // filter out our own debug messages from the log.
  zx_info_handle_basic_t info;
  if (zx_object_get_info(zx_process_self(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL,
                         NULL) == ZX_OK) {
    proc_koid = info.koid;
  }

  log_wait.emplace(read_only_debuglog.release(), ZX_LOG_READABLE, 0, log_reader_cb);
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

static void log_reader_cb(async_dispatcher_t* dispatcher, async::Wait* wait,
                          zx_status_t wait_status, const zx_packet_signal_t* signal) {
  char buf[ZX_LOG_RECORD_MAX];
  zx_log_record_t* rec = (zx_log_record_t*)buf;
  zx_status_t status;
  for (;;) {
    status = zx_debuglog_read(wait->object(), 0, rec, ZX_LOG_RECORD_MAX);
    // zx_debuglog_read returns >0 for success.
    if (status < 0) {
      break;
    }
    // Don't print log messages from ourself.
    if (rec->pid == proc_koid) {
      continue;
    }
    write_to_log(g_log_vc, rec);

    vc_t* vc = NULL;
    list_for_every_entry (&log_list, vc, vc_t, node) { write_to_log(vc, rec); }
  }

  if (status != ZX_ERR_SHOULD_WAIT) {
    const char* oops = "<<LOG ERROR>>\n";
    vc_write(g_log_vc, oops, strlen(oops), 0);
  }

  // Queue up another read.
  wait->Begin(dispatcher);
}
