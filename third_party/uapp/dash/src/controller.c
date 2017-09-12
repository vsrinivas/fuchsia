// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <linenoise/linenoise.h>

static zx_handle_t ctrl_channel = ZX_HANDLE_INVALID;

// Maximum length of a history entry, including the ending '\n'.
static const size_t kMaxHistoryEntrySize = 1024;
static const char kGetHistoryCommand[] = "get_history";
static const size_t kGetHistoryCommandLen = sizeof(kGetHistoryCommand) - 1;
static const char kAddLocalEntryCommand[] = "add_local_entry:";
static const size_t kAddLocalEntryCommandLen = sizeof(kAddLocalEntryCommand) - 1;
static const char kAddRemoteEntryCommand[] = "add_remote_entry:";
static const size_t kAddRemoteEntryCommandLen = sizeof(kAddRemoteEntryCommand) - 1;

void controller_init() {
  ctrl_channel = zx_get_startup_handle(PA_HND(PA_USER1, 0));

  if (ctrl_channel == ZX_HANDLE_INVALID) {
    // Running without a shell controller.
    return;
  }

  // Initialize the shell history.
  zx_status_t status = zx_channel_write(ctrl_channel, 0, kGetHistoryCommand,
                                        kGetHistoryCommandLen, NULL, 0);
  if (status != ZX_OK) {
    fprintf(stderr,
            "Failed to write the get_history command to the ctrl channel.\n");
    return;
  }

  status = zx_object_wait_one(ctrl_channel,
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                              zx_deadline_after(ZX_SEC(5)), NULL);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to wait on the ctrl channel.\n");
    return;
  }

  zx_handle_t history_vmo;
  uint32_t read_bytes = 0;
  uint32_t read_handles = 0;
  status = zx_channel_read(ctrl_channel, 0, NULL, &history_vmo, 0,
                           1, &read_bytes, &read_handles);
  if (status != ZX_OK) {
    fprintf(stderr,
            "Failed to read the ctrl response to the get_history command.\n");
    return;
  }

  uint64_t history_vmo_size = 0;
  status = zx_vmo_get_size(history_vmo, &history_vmo_size);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get the size of the history vmo.\n");
    return;
  }

  uint64_t history_vmo_offset = 0;
  while (history_vmo_offset < history_vmo_size) {
    char buffer[kMaxHistoryEntrySize];
    size_t actually_read = 0;
    status = zx_vmo_read(history_vmo, &buffer, history_vmo_offset,
                         sizeof(buffer), &actually_read);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read from the history vmo.\n");
      return;
    }

    // We only move |history_vmo_offset| to the next byte after the previous
    // '\n', so the beginning of buffer should always be the beginning of a new
    // entry.
    char* entry_start = buffer;
    char* const buffer_end = buffer + actually_read;
    bool found_an_entry = false;
    for (char* it = buffer; it < buffer_end; it++) {
      if (*it == '\n') {
        *it = '\0';
        linenoiseHistoryAdd(entry_start);
        history_vmo_offset += it + 1 - entry_start;
        entry_start = it + 1;
        found_an_entry = true;
      }
    }

    if (!found_an_entry) {
      fprintf(stderr, "Incorrect format of the history vmo.\n");
      return;
    }
  }
}

void controller_add_local_entry(const char* entry, size_t length) {
  if (ctrl_channel == ZX_HANDLE_INVALID || length > kMaxHistoryEntrySize) {
    return;
  }
  char buffer[kAddLocalEntryCommandLen + kMaxHistoryEntrySize];
  memcpy(buffer, kAddLocalEntryCommand, kAddLocalEntryCommandLen);
  memcpy(buffer + kAddLocalEntryCommandLen, entry, length);
  zx_status_t status = zx_channel_write(ctrl_channel, 0,
                                        buffer, kAddLocalEntryCommandLen + length,
                                        NULL, 0);
  if (status != ZX_OK) {
    fprintf(stderr,
            "Failed to write the add_to_history command to the ctrl channel\n");
    zx_handle_close(ctrl_channel);
    ctrl_channel = ZX_HANDLE_INVALID;
    return;
  }
}

void controller_pull_remote_entries() {
  if (ctrl_channel == ZX_HANDLE_INVALID) {
    return;
  }

  // The commands should not be bigger than the name of the command + max size
  // of a history entry.
  char buffer[kMaxHistoryEntrySize + 100];

  while(true) {
    uint32_t read_bytes = 0;
    zx_status_t status = zx_channel_read(ctrl_channel, 0, buffer, NULL,
                                         sizeof(buffer) - 1, 0, &read_bytes, NULL);
    if (status == ZX_OK) {
      if (strncmp(buffer, kAddRemoteEntryCommand, kAddRemoteEntryCommandLen) != 0) {
        fprintf(stderr, "Unrecognized shell controller command.\n");
        continue;
      }
      buffer[read_bytes] = '\0';
      const char* start = buffer + kAddRemoteEntryCommandLen;
      linenoiseHistoryAdd(start);
    } else if (status == ZX_ERR_SHOULD_WAIT) {
      return;
    } else {
      fprintf(stderr, "Failed to read the command from the ctrl channel, status: %u.\n", status);
      return;
    }
  }

}

#ifdef mkinit
INCLUDE "controller.h"
INIT {
  controller_init();
}
#endif
