// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tftp.h"

#include <lib/fdio/spawn.h>
#include <zircon/boot/netboot.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <cstddef>
#include <cstdio>

#include <inet6/inet6.h>
#include <tftp/tftp.h>

#include "file-api.h"
#include "netsvc.h"

#define SCRATCHSZ 2048

extern bool xfer_active;

// Visible for test injection.
netsvc::FileApiInterface* g_file_api = nullptr;

namespace {

struct transport_info_t {
  ip6_addr_t dest_addr;
  uint16_t dest_port;
  uint32_t timeout_ms;
};

char tftp_session_scratch[SCRATCHSZ];
char tftp_out_scratch[SCRATCHSZ];

size_t last_msg_size = 0;
tftp_session* session = nullptr;
transport_info_t transport_info;

zx_time_t g_tftp_next_timeout = ZX_TIME_INFINITE;

ssize_t file_open_read(const char* filename, void* cookie) {
  auto* file_api = reinterpret_cast<netsvc::FileApiInterface*>(cookie);
  return file_api->OpenRead(filename);
}

tftp_status file_open_write(const char* filename, size_t size, void* cookie) {
  auto* file_api = reinterpret_cast<netsvc::FileApiInterface*>(cookie);
  return file_api->OpenWrite(filename, size);
}

tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
  auto* file_api = reinterpret_cast<netsvc::FileApiInterface*>(cookie);
  return file_api->Read(data, length, offset);
}

tftp_status file_write(const void* data, size_t* length, off_t offset, void* cookie) {
  auto* file_api = reinterpret_cast<netsvc::FileApiInterface*>(cookie);
  return file_api->Write(data, length, offset);
}

void file_close(void* cookie) {
  auto* file_api = reinterpret_cast<netsvc::FileApiInterface*>(cookie);
  return file_api->Close();
}

tftp_status transport_send(void* data, size_t len, void* transport_cookie) {
  transport_info_t* transport_info = reinterpret_cast<transport_info_t*>(transport_cookie);
  zx_status_t status = udp6_send(data, len, &transport_info->dest_addr, transport_info->dest_port,
                                 NB_TFTP_OUTGOING_PORT, true);
  if (status != ZX_OK) {
    return TFTP_ERR_IO;
  }

  // The timeout is relative to sending instead of receiving a packet, since there are some
  // received packets we want to ignore (duplicate ACKs).
  if (transport_info->timeout_ms != 0) {
    g_tftp_next_timeout = zx_deadline_after(ZX_MSEC(transport_info->timeout_ms));
  }
  return TFTP_NO_ERROR;
}

int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
  transport_info_t* transport_info = reinterpret_cast<transport_info_t*>(transport_cookie);
  transport_info->timeout_ms = timeout_ms;
  return 0;
}

void initialize_connection(const ip6_addr_t* saddr, uint16_t sport) {
  int ret = tftp_init(&session, tftp_session_scratch, sizeof(tftp_session_scratch));
  if (ret != TFTP_NO_ERROR) {
    printf("netsvc: failed to initiate tftp session\n");
    session = nullptr;
    return;
  }

  if (g_file_api == nullptr) {
    g_file_api = new netsvc::FileApi(netbootloader());
  }

  // Initialize file interface
  tftp_file_interface file_ifc = {file_open_read, file_open_write, file_read, file_write,
                                  file_close};
  tftp_session_set_file_interface(session, &file_ifc);

  // Initialize transport interface
  memcpy(&transport_info.dest_addr, saddr, sizeof(ip6_addr_t));
  transport_info.dest_port = sport;
  transport_info.timeout_ms = TFTP_TIMEOUT_SECS * 1000;
  tftp_transport_interface transport_ifc = {transport_send, NULL, transport_timeout_set};
  tftp_session_set_transport_interface(session, &transport_ifc);

  xfer_active = true;
}

void end_connection() {
  session = nullptr;
  g_tftp_next_timeout = ZX_TIME_INFINITE;
  xfer_active = false;
}

void report_metrics() {
  char buf[256];
  if (session && tftp_get_metrics(session, buf, sizeof(buf)) == TFTP_NO_ERROR) {
    printf("netsvc: metrics: %s\n", buf);
  }
}

}  // namespace

zx_time_t tftp_next_timeout() { return g_tftp_next_timeout; }

void tftp_timeout_expired() {
  tftp_status result =
      tftp_timeout(session, tftp_out_scratch, &last_msg_size, sizeof(tftp_out_scratch),
                   &transport_info.timeout_ms, g_file_api);
  if (result == TFTP_ERR_TIMED_OUT) {
    printf("netsvc: excessive timeouts, dropping tftp connection\n");
    g_file_api->Abort();
    end_connection();
  } else if (result < 0) {
    printf("netsvc: failed to generate timeout response, dropping tftp connection\n");
    g_file_api->Abort();
    end_connection();
  } else {
    if (last_msg_size > 0) {
      tftp_status send_result = transport_send(tftp_out_scratch, last_msg_size, &transport_info);
      if (send_result != TFTP_NO_ERROR) {
        printf("netsvc: failed to send tftp timeout response (err = %d)\n", send_result);
      }
    }
  }
}

void tftp_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
  if (dport == NB_TFTP_INCOMING_PORT) {
    if (session != NULL) {
      printf("netsvc: only one simultaneous tftp session allowed\n");
      // ignore attempts to connect when a session is in progress
      return;
    }
    initialize_connection(saddr, sport);
  } else if (!session) {
    // Ignore anything sent to the outgoing port unless we've already
    // established a connection.
    return;
  }

  last_msg_size = sizeof(tftp_out_scratch);

  char err_msg[128];
  tftp_handler_opts handler_opts = {.inbuf = reinterpret_cast<char*>(data),
                                    .inbuf_sz = len,
                                    .outbuf = tftp_out_scratch,
                                    .outbuf_sz = &last_msg_size,
                                    .err_msg = err_msg,
                                    .err_msg_sz = sizeof(err_msg)};
  tftp_status status = tftp_handle_msg(session, &transport_info, g_file_api, &handler_opts);
  switch (status) {
    case TFTP_NO_ERROR:
      return;
    case TFTP_TRANSFER_COMPLETED:
      printf("netsvc: tftp %s of file %s completed\n", g_file_api->is_write() ? "write" : "read",
             g_file_api->filename());
      report_metrics();
      break;
    case TFTP_ERR_SHOULD_WAIT:
      break;
    default:
      printf("netsvc: %s\n", err_msg);
      g_file_api->Abort();
      report_metrics();
      break;
  }
  end_connection();
}

bool tftp_has_pending() { return session && tftp_session_has_pending(session); }

void tftp_send_next() {
  last_msg_size = sizeof(tftp_out_scratch);
  tftp_prepare_data(session, tftp_out_scratch, &last_msg_size, &transport_info.timeout_ms,
                    g_file_api);
  if (last_msg_size) {
    transport_send(tftp_out_scratch, last_msg_size, &transport_info);
  }
}
