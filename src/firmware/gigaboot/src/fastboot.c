// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define DEBUG_LOGGING 0

#define _POSIX_C_SOURCE 200809L  // Include strnlen() in string.h

#include "fastboot.h"

#include <bootbyte.h>
#include <inttypes.h>
#include <lib/abr/data.h>
#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>
#include <zircon/hw/gpt.h>

#include "abr.h"
#include "bootbyte.h"
#include "bootimg.h"
#include "diskio.h"
#include "inet6.h"
#include "mdns.h"
#include "netifc.h"
#include "tcp.h"
#include "util.h"
#include "zircon.h"

// Fastboot protocol constants.
// Fastboot client will send at most 64 byte commands. Newer versions of
// fastboot can accept up to 256 byte responses, but we don't send anything
// that large so just keep both at 64 for now for better compatibility.
#define FB_CMD_MAX_LEN 64
#define FB_PROTOCOL_VERSION 4
#define FB_UDP_HDR_SIZE 4
#define FB_TCP_PROTOCOL_VERSION 1

// Constants specific to our implementation.
#define DEBUG 0
#define NUM_COMMANDS 9
#define NUM_VARIABLES 14
#define PAGE_SIZE 4096
#define PARTITION_OFFSET 0
#define UDP_MAX_PAYLOAD_SIZE (UDP6_MAX_PAYLOAD - FB_UDP_HDR_SIZE)
#define UDP_INITIAL_SEQ_NUM 0x55aa
// If this bit is set in fb_tcp_state, we're using the TCP drivers and must not
// read packets from the network manually (writing is OK).
#define TCP_STATE_ACTIVE_FLAG (0x80)

// Enumeration of the types of packets allowed in the fastboot protocol.
typedef enum {
  ERROR_TYPE = 0x00,
  QUERY_TYPE = 0x01,
  INIT_TYPE = 0x02,
  FASTBOOT_TYPE = 0x03
} pkt_type;

// Enumeration of the phase a fastboot command is in.
typedef enum { IDLE = 0, CMD = 1, DATA = 2, ALLVAR = 3 } fb_cmd_phase;

// Current TCP state.
typedef enum {
  TCP_STATE_INITIALIZE,  // Bringing up the TCP stack.
  TCP_STATE_IDLE,        // No TCP activity.
  TCP_STATE_ERROR,       // Fatal error, stop trying to use TCP.

  TCP_STATE_CONNECT = TCP_STATE_ACTIVE_FLAG,  // Waiting for a client to connect.
  TCP_STATE_HANDSHAKE_RX,                     // Reading the handshake packet.
  TCP_STATE_READ_HEADER,                      // Reading a packet header.
  TCP_STATE_READ_DATA,                        // Reading packet data.
  TCP_STATE_WRITE,                            // Writing a packet.
  TCP_STATE_DISCONNECT                        // Disconnecting a client.
} tcp_state;

// Types.
// Fastboot packet.
typedef struct {
  uint8_t pkt_id;
  uint8_t pkt_flags;
  uint16_t seq_num;
  uint8_t data[UDP_MAX_PAYLOAD_SIZE];
} fb_udp_pkt_t;

// A UDP destination address.
typedef struct {
  const void *daddr;
  uint16_t dport;
  uint16_t sport;
} udp_addr_t;

// fb_cmd_t represents a fastboot command, and contains a function to both
// execute the command and send a response to the host.
typedef struct {
  const char *name;
  void (*func)(char *cmd);
} fb_cmd_t;

// fb_var contains the name of a fastboot variable, along with either a
// constant value or a function that can get it. This function places the result
// in the second argument, and returns 0 on success, -1 on failure.
typedef struct {
  const char *name;
  const char *value;
  int (*func)(const char *arg, char *result);
  const char **default_args;
} fb_var_t;

// fb_img_t represents an in memory download image.
typedef struct {
  uint32_t size;
  uint32_t bytes_received;
  void *data;
} fb_img_t;

// Function prototypes.
// Helpers.
void pp_fb_pkt(const char *direction, fb_udp_pkt_t *pkt, size_t len);
void fb_send_data(const char *msg);
void fb_send_okay(const char *msg);
void fb_send_info(const char *msg);
void fb_send_fail(const char *msg);
void fb_send_ack(void);
void fb_resend(void);

// The main fastboot engine.
//
// To be called each time it is our turn to take action. This usually means we
// just received a packet from the host, but not always - for example, we may
// call this repeatedly to send consecutive INFO packets.
//
// Args:
//   data: received packet data, or NULL to just advance the engine.
//   len: data length.
void fb_engine(const void *data, size_t len);

// Functions that respond to each packet type.
void respond_to_init_packet(fb_udp_pkt_t *pkt);
void respond_to_query_packet(fb_udp_pkt_t *pkt);

// Fastboot command functions. These functions execute a command and send
// results/responses to the host.
void fb_reboot(char *cmd);
void fb_flash(char *cmd);
void fb_erase(char *cmd);
void fb_download(char *cmd);
void fb_getvar(char *cmd);
void fb_set_active(char *cmd);
void fb_boot(char *cmd);
void fb_continue(char *cmd);
void fb_staged_bootloader_file(char *cmd);

// Fastboot variable functions. These functions retreive a variable and return
// the value as a null terminated string. They are responsible for sending
// failures to the host when they encountered.
int get_max_download_size(const char *arg, char *result);
int get_current_slot(const char *arg, char *result);
int get_slot_unbootable(const char *slot, char *result);
int get_slot_successful(const char *slot, char *result);
int get_slot_retry_count(const char *slot, char *result);

// Global state.
static uint16_t max_pkt_size;
static udp_addr_t dest_addr;
static fb_udp_pkt_t pkt_to_send;
static size_t pkt_to_send_len;
static char curr_cmd[FB_CMD_MAX_LEN + 1];  // Add space for null terminator.
static uint16_t expected_seq_num = UDP_INITIAL_SEQ_NUM;
static fb_img_t curr_img;
static fb_cmd_phase cmd_phase;
static uint8_t curr_var_idx;
static uint8_t curr_var_arg_idx;
static const char *slot_suffix_list[] = {"a", "b", NULL};
static fb_bootimg_t boot_img;
static fb_poll_next_action fb_poll_action = 0;
static fb_udp_poll_func_t udp_poll_func = netifc_poll;
static fb_udp6_send_func_t udp6_send_func = udp6_send;

void fb_set_udp_functions_for_testing(fb_udp_poll_func_t poll_func, fb_udp6_send_func_t send_func) {
  udp_poll_func = (poll_func ? poll_func : netifc_poll);
  udp6_send_func = (send_func ? send_func : udp6_send);
}

static tcp_state fb_tcp_state = TCP_STATE_INITIALIZE;
static tcp6_socket fb_tcp_socket = {};

// TCP read/write buffer for standard commands/responses (+1 for null term).
//
// Downloads will read into |curr_img| instead which is dynamically allocated
// so it can hold the complete image.
//
// Currently we only ever do one of read or write at a time, so they can share
// the same buffer.
static uint8_t fb_tcp_buffer[FB_CMD_MAX_LEN + 1];

// TCP packet read/write length.
//
// Technically the fastboot TCP protocol supports 64-bit lengths, but our TCP
// APIs do not.
static uint32_t fb_tcp_length = 0;

void fb_reset_tcp_state_for_testing(void) {
  fb_tcp_state = TCP_STATE_INITIALIZE;
  memset(&fb_tcp_socket, 0, sizeof(fb_tcp_socket));
  fb_tcp_length = 0;
}

// cmdlist maps a command name to the function that handles that command.
static fb_cmd_t cmdlist[NUM_COMMANDS] = {
    {
        // This command handles (-recovery|-bootloader) as well.
        .name = "reboot",
        .func = fb_reboot,
    },
    {
        .name = "flash",
        .func = fb_flash,
    },
    {
        .name = "erase",
        .func = fb_erase,
    },
    {
        .name = "download",
        .func = fb_download,
    },
    {
        .name = "getvar",
        .func = fb_getvar,
    },
    {
        .name = "set_active",
        .func = fb_set_active,
    },
    {
        .name = "boot",
        .func = fb_boot,
    },
    {
        .name = "continue",
        .func = fb_continue,
    },
    {
        .name = "oem add-staged-bootloader-file",
        .func = fb_staged_bootloader_file,
    },
};

// varlist contains all variables this bootloader supports.
static fb_var_t varlist[NUM_VARIABLES] = {
    {
        .name = "has-slot",
        .value = "",
    },
    {
        .name = "partition-type",
        .value = "",
    },
    {
        .name = "max-download-size",
        .func = get_max_download_size,
    },
    {
        .name = "is-logical",
        .value = "no",
    },
    {
        .name = "slot-count",
        .value = "2",
    },
    {
        .name = "bootloader-min-versions",
        .value = "0",
    },
    {
        .name = "current-slot",
        .func = get_current_slot,
    },
    {
        // `ffx flash` requires that "hw-revision" matches the board name.
        .name = "hw-revision",
        .value = BOARD_NAME,
    },
    {
        .name = "product",
        .value = "gigaboot",
    },
    {
        .name = "serialno",
        .value = "unimplemented",
    },
    {
        .name = "slot-retry-count",
        .func = get_slot_retry_count,
        .default_args = slot_suffix_list,
    },
    {
        .name = "slot-successful",
        .func = get_slot_successful,
        .default_args = slot_suffix_list,
    },
    {
        .name = "slot-unbootable",
        .func = get_slot_unbootable,
        .default_args = slot_suffix_list,
    },
    {
        .name = "version",
        .value = "0.4",
    },
};

// It seems that TCP initialization sometimes fails early on but succeeds later.
// We don't get much info from the driver, but I suspect that it needs an active
// link to initialize properly, so set a timer to keep trying every few seconds.
static void fb_tcp_initialize(void) {
  static efi_event init_timer = NULL;

  // Try to connect immediately, and whenever the timer fires.
  if (!init_timer || gBS->CheckEvent(init_timer) == EFI_SUCCESS) {
    DLOG("FB TCP init attempt");
    // Use the link-local IP address synthesized from our MAC.
    // TODO: remove our custom IP6 type so that everything just uses
    // efi_ipv6_addr directly and we don't have to convert here.
    static_assert(sizeof(efi_ipv6_addr) == sizeof(ll_ip6_addr), "IP6 address size mismatch");
    efi_ipv6_addr efi_ll_addr;
    memcpy(&efi_ll_addr, &ll_ip6_addr, sizeof(ll_ip6_addr));
    if (tcp6_open(&fb_tcp_socket, gBS, &efi_ll_addr, FB_SERVER_PORT) == TCP6_RESULT_SUCCESS) {
      LOG("Fastboot TCP is ready");
      fb_tcp_state = TCP_STATE_IDLE;
      if (init_timer) {
        gBS->CloseEvent(init_timer);
        init_timer = NULL;
      }
      return;
    }
    DLOG("Fastboot TCP init failure, will try again in a few seconds");
  }

  if (init_timer == NULL) {
    DLOG("Starting TCP init timer");
    efi_status status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &init_timer);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to create TCP init timer");
      fb_tcp_state = TCP_STATE_ERROR;
      return;
    }

    // Try to initialize every 2 seconds (units of 100ns).
    status = gBS->SetTimer(init_timer, TimerPeriodic, 2ULL * 10 * 1000 * 1000);
    if (status != EFI_SUCCESS) {
      ELOG_S(status, "Failed to start TCP init timer");
      fb_tcp_state = TCP_STATE_ERROR;
      return;
    }
  }
}

// Waits for a TCP client to connect.
static tcp6_result fb_tcp_connect(void) {
  // TODO: add some timeout here - if we get a single TCP packet but no client
  // ever connects, we will just stay in this state forever. Instead if nobody
  // connects after a few seconds we should return to IDLE state so UDP packet
  // processing can resume.
  tcp6_result result = tcp6_accept(&fb_tcp_socket);
  if (result == TCP6_RESULT_SUCCESS) {
    fb_tcp_state = TCP_STATE_HANDSHAKE_RX;
  }
  return result;
}

// Reads the handshake packet from the client.
static tcp6_result fb_tcp_handshake_rx(void) {
  // Handshake is 4 bytes - "FB" then a 2-digit base 10 ASCII version.
  tcp6_result result = tcp6_read(&fb_tcp_socket, fb_tcp_buffer, 4);
  if (result == TCP6_RESULT_SUCCESS) {
    if (memcmp(fb_tcp_buffer, "FB", 2) != 0) {
      WLOG("Unexpected FB TCP handshake RX; disconnecting");
      return TCP6_RESULT_ERROR;
    }

    // Make sure we can agree on protocol version. It's OK if the client has a
    // higher version, they have to revert to our lower version or disconnect
    // if they can't.
    fb_tcp_buffer[4] = '\0';
    int client_version = atoi((const char *)&fb_tcp_buffer[2]);
    if (client_version < FB_TCP_PROTOCOL_VERSION) {
      WLOG("Unsupported FB TCP protocol version: %d\n", client_version);
      return TCP6_RESULT_ERROR;
    }

    fb_tcp_length = sprintf((char *)fb_tcp_buffer, "FB%02d", FB_TCP_PROTOCOL_VERSION);
    fb_tcp_state = TCP_STATE_WRITE;
  }
  return result;
}

// Reads a packet header from the client.
static tcp6_result fb_tcp_read_header(void) {
  // TCP header is just an unsigned 64-bit big-endian value indicating the
  // packet data length.
  uint64_t length;

  tcp6_result result = tcp6_read(&fb_tcp_socket, fb_tcp_buffer, sizeof(length));
  if (result == TCP6_RESULT_SUCCESS) {
    memcpy(&length, fb_tcp_buffer, sizeof(length));
    length = ntohll(length);
    DLOG("FB TCP incoming packet length: %" PRIu64, length);

    // UEFI TCP6 read only supports 32-bit length. We can perform multiple
    // reads if needed, but for the time being no fastboot packet is going
    // to be > 4GiB so just do a quick check here instead.
    if (length > UINT32_MAX) {
      ELOG("FB TCP packet size too large (%" PRIu64 "), disconnecting", length);
      return TCP6_RESULT_ERROR;
    }

    // If we're in DATA phase we read directly into an allocated buffer,
    // otherwise it needs to fit in our static buffer.
    size_t buffer_size = (cmd_phase == DATA ? curr_img.size : sizeof(fb_tcp_buffer));
    if (length > buffer_size) {
      ELOG("FB TCP data exceeds read buffer size (%u > %zu)", fb_tcp_length, buffer_size);
      return TCP6_RESULT_ERROR;
    }

    fb_tcp_length = (uint32_t)length;
    fb_tcp_state = TCP_STATE_READ_DATA;
  }
  return result;
}

// Reads packet data from the client.
static tcp6_result fb_tcp_read_data(void) {
  tcp6_result result;

  if (cmd_phase == DATA) {
    // In DATA phase, read directly into our allocated curr_img buffer.
    uint32_t data_remaining = curr_img.size - curr_img.bytes_received;
    if (fb_tcp_length > data_remaining) {
      ELOG("FB TCP RX data exceeds remaining image size (%u > %u)", fb_tcp_length, data_remaining);
      return TCP6_RESULT_ERROR;
    }

    result = tcp6_read(&fb_tcp_socket, (uint8_t *)curr_img.data + curr_img.bytes_received,
                       fb_tcp_length);

    // For DATA phase only, the image might be spread over multiple TCP messages
    // (looks like currently fastboot limits to 512MiB per message). If there's
    // more image data to come, start reading again.
    if (result == TCP6_RESULT_SUCCESS) {
      curr_img.bytes_received += fb_tcp_length;
      if (curr_img.bytes_received != curr_img.size) {
        fb_tcp_state = TCP_STATE_READ_HEADER;
        return TCP6_RESULT_SUCCESS;
      }
    }
  } else {
    result = tcp6_read(&fb_tcp_socket, fb_tcp_buffer, fb_tcp_length);
  }

  if (result == TCP6_RESULT_SUCCESS) {
    DLOG("FB TCP packet received");
    fb_engine(fb_tcp_buffer, fb_tcp_length);
  }

  return result;
}

// Writes a packet to the client.
static tcp6_result fb_tcp_write(void) {
  tcp6_result result = tcp6_write(&fb_tcp_socket, fb_tcp_buffer, fb_tcp_length);
  if (result == TCP6_RESULT_SUCCESS) {
    if (cmd_phase == ALLVAR) {
      // ALLVAR special case, we send several INFO messages in a row so it's
      // still our turn to transmit. fb_engine() queues the next one up.
      fb_engine(NULL, 0);
    } else {
      fb_tcp_state = TCP_STATE_READ_HEADER;
      return TCP6_RESULT_SUCCESS;
    }
  }
  return result;
}

// Disconnects the client.
static tcp6_result fb_tcp_disconnect(void) {
  tcp6_result result = tcp6_disconnect(&fb_tcp_socket);
  if (result == TCP6_RESULT_SUCCESS) {
    fb_tcp_state = TCP_STATE_IDLE;
  }
  return result;
}

// Fastboot TCP main loop.
//
// Non-blocking, we still want to return to the main loop to service other
// periodic tasks (e.g. mDNS broadcasts).
static void fb_tcp_tick(void) {
  tcp6_result result;

  switch (fb_tcp_state) {
    case TCP_STATE_CONNECT:
      result = fb_tcp_connect();
      break;
    case TCP_STATE_HANDSHAKE_RX:
      result = fb_tcp_handshake_rx();
      break;
    case TCP_STATE_READ_HEADER:
      result = fb_tcp_read_header();
      break;
    case TCP_STATE_READ_DATA:
      result = fb_tcp_read_data();
      break;
    case TCP_STATE_WRITE:
      result = fb_tcp_write();
      break;
    case TCP_STATE_DISCONNECT:
      result = fb_tcp_disconnect();
      break;
    default:
      ELOG("Unexpected fb_tcp_state: 0x%X", fb_tcp_state);
      fb_tcp_state = TCP_STATE_ERROR;
      return;
  }

  // On client disconnect or error, disconnect our side and start over. The
  // only difference is the logging level, since client disconnect is expected
  // in normal operation whenever the host is done.
  if (result == TCP6_RESULT_DISCONNECTED) {
    DLOG("FB TCP client disconnected");
    fb_tcp_state = TCP_STATE_DISCONNECT;
  } else if (result == TCP6_RESULT_ERROR) {
    ELOG("FB TCP error in state 0x%X, disconnecting", fb_tcp_state);
    fb_tcp_state = TCP_STATE_DISCONNECT;
  }
}

fb_poll_next_action fb_poll(fb_bootimg_t *img) {
  if (fb_poll_action != POLL) {
    // If we're done with fastboot but the TCP session is not yet closed, just
    // continue ticking until it closes. This is important so that the fastboot
    // client receives the final OKAY message, otherwise it will hang.
    if (fb_tcp_state & TCP_STATE_ACTIVE_FLAG) {
      fb_tcp_tick();
      return POLL;
    }

    // The TCP session is closed, we can now move on to whatever our final
    // action is, but reset |fb_poll_action| so that if it fails we can get back
    // into the normal fastboot loop.
    fb_poll_next_action next_action = fb_poll_action;
    fb_poll_action = POLL;
    return next_action;
  }

  if (fb_tcp_state == TCP_STATE_INITIALIZE) {
    fb_tcp_initialize();
  }

  if (fb_tcp_state & TCP_STATE_ACTIVE_FLAG) {
    fb_tcp_tick();
  } else {
    udp_poll_func();
  }

  if (fb_poll_action == BOOT_FROM_RAM) {
    memcpy((void *)img, (void *)&boot_img, sizeof(fb_bootimg_t));
  }

  // Always return POLL here so that we can continue to tick if the TCP session
  // still needs to finish up, the logic at the beginning of this function will
  // return the final non-POLL value.
  return POLL;
}

// TODO: consider just switching into TCP mode permanently once the client has
// demonstrated the ability to speak the TCP protocol. It's much faster, we
// could avoid subsequent mode switch delays, and going back to UDP after using
// TCP sometimes appears to increase UDP flakiness.
void fb_tcp_recv(void) {
  // If TCP is ready, start listening for a connection.
  if (fb_tcp_state == TCP_STATE_IDLE) {
    DLOG("Got a FB TCP packet, switching to TCP mode");
    fb_tcp_state = TCP_STATE_CONNECT;
  } else {
    DLOG("Got a FB TCP packet, but TCP isn't available; ignoring");
  }
}

bool fb_tcp_is_available(void) {
  // Fastboot-over-TCP is available if we're idling waiting for a connection or
  // if there is currently an active connection.
  return (fb_tcp_state == TCP_STATE_IDLE) || (fb_tcp_state & TCP_STATE_ACTIVE_FLAG);
}

// fb_recv runs every time a UDP packet destined for the fastboot port is
// received.
void fb_recv(void *data, size_t len, const void *saddr, uint16_t sport) {
  if (len > sizeof(fb_udp_pkt_t)) {
    fb_send_fail("received fastboot packet larger than max packet size");
    return;
  }
  fb_udp_pkt_t *pkt = (fb_udp_pkt_t *)data;
  if (DEBUG) {
    pp_fb_pkt("host", pkt, len);
  }
  uint16_t cur_seq_num = ntohs(pkt->seq_num);

  // Prepare the destination address.
  dest_addr.daddr = saddr;
  dest_addr.dport = sport;
  dest_addr.sport = FB_SERVER_PORT;

  if (pkt->pkt_id == QUERY_TYPE) {
    // Clear the last response.
    memset(&pkt_to_send, 0, sizeof(pkt_to_send));
    pkt_to_send.pkt_id = pkt->pkt_id;
    pkt_to_send.seq_num = pkt->seq_num;
    pkt_to_send.pkt_flags = 0;

    respond_to_query_packet(pkt);
  } else if (cur_seq_num == expected_seq_num) {
    // Clear the last response.
    memset(&pkt_to_send, 0, sizeof(pkt_to_send));
    pkt_to_send.pkt_id = pkt->pkt_id;
    pkt_to_send.seq_num = pkt->seq_num;
    pkt_to_send.pkt_flags = 0;

    if (pkt->pkt_id == INIT_TYPE) {
      respond_to_init_packet(pkt);
      // Reset the command phase.
      cmd_phase = IDLE;
    } else if (pkt->pkt_id == FASTBOOT_TYPE) {
      fb_engine(pkt->data, len - FB_UDP_HDR_SIZE);
    } else if (pkt->pkt_id == ERROR_TYPE) {
      LOG("got error from host: %s", (char *)(pkt->data));
    } else {
      // Send an error to the host.
      pkt_to_send.pkt_id = ERROR_TYPE;
      snprintf((char *)pkt_to_send.data, UDP_MAX_PAYLOAD_SIZE,
               "fastboot packet had malformed type %#02x", pkt->pkt_id);
      udp6_send_func((void *)&pkt_to_send,
                     FB_UDP_HDR_SIZE + strnlen((char *)pkt_to_send.data, UDP_MAX_PAYLOAD_SIZE),
                     dest_addr.daddr, dest_addr.dport, dest_addr.sport);
      ELOG("malformed type: %#02x", pkt->pkt_id);
      return;
    }
    expected_seq_num += 1;
  } else if (cur_seq_num == expected_seq_num - 1) {
    fb_resend();
  }
}

void fb_engine(const void *data, size_t len) {
  switch (cmd_phase) {
    case IDLE: {
      memcpy((void *)curr_cmd, data, len);
      // Ensure that the current command is null terminated, as we will depend
      // on this to tokenize later.
      curr_cmd[len] = '\0';
      cmd_phase = CMD;
      // Handle the "getvar:all" special case, as it requires multi packet
      // interaction.
      if (!strncmp(curr_cmd, "getvar:all", strlen("getvar:all"))) {
        cmd_phase = ALLVAR;
        curr_var_idx = 0;
        curr_var_arg_idx = 0;
      }

      // Fastboot UDP does not support combined ACK + response packets, so we
      // need to just ACK here and then wait for the host to send the next
      // (empty) packet, which will trigger this function again.
      //
      // TCP handles ACKs internally, so we can just call this function right
      // now to start the response transmission.
      if (fb_tcp_state & TCP_STATE_ACTIVE_FLAG) {
        // |data| and |len| have already been saved to |curr_cmd|, we don't
        // need them anymore.
        fb_engine(NULL, 0);
      } else {
        fb_send_ack();
      }
      break;
    }
    case CMD: {
      // Generally, we transition to the IDLE phase after handling a CMD.
      cmd_phase = IDLE;
      bool found = false;
      for (int i = 0; i < NUM_COMMANDS; i++) {
        fb_cmd_t cmd = cmdlist[i];
        // strlen is safe here because the cmd name is specified as a constant
        // above.
        if (!strncmp(curr_cmd, cmd.name, strlen(cmd.name))) {
          found = true;
          cmd.func(curr_cmd);
          break;
        }
      }
      if (!found) {
        fb_send_fail("command not found");
      }
      // Clear the current command.
      memset(curr_cmd, '\0', FB_CMD_MAX_LEN);
      break;
    }
    case DATA: {
      if (curr_img.bytes_received == curr_img.size) {
        fb_send_okay("");
        cmd_phase = IDLE;
      } else {
        // UDP only; TCP always reads the full image directly into |curr_img| to
        // avoid unnecessary copying.

        // Keep copying data from the host until we've received all of it.
        memcpy(curr_img.data + curr_img.bytes_received, data, len);
        curr_img.bytes_received += len;

        // Send an ACK to tell the host we received the data.
        fb_send_ack();
      }
      break;
    }
    case ALLVAR: {
      if (curr_var_idx == NUM_VARIABLES) {
        // If we've gone through all of our variables, send an OKAY and return
        // to IDLE.
        cmd_phase = IDLE;
        fb_send_okay("");
        return;
      }
      fb_var_t var = varlist[curr_var_idx];
      char allvar_result[FB_CMD_MAX_LEN];
      if (var.value) {
        snprintf(allvar_result, FB_CMD_MAX_LEN, "%s:%s", var.name, var.value);
        fb_send_info(allvar_result);
        curr_var_idx += 1;
      } else {
        const char *arg = NULL;
        if (var.default_args) {
          arg = var.default_args[curr_var_arg_idx];
        }
        char result[FB_CMD_MAX_LEN];
        memset(result, 0, FB_CMD_MAX_LEN);  // Zero out to null terminate.
        if (var.func(arg, result) == 0) {
          // Since the variable was successfully retrieved, generate the
          // formatted key:value pair response and send.
          if (arg) {
            snprintf(allvar_result, sizeof(allvar_result), "%s:%s:%s", var.name, arg, result);
          } else {
            snprintf(allvar_result, sizeof(allvar_result), "%s:%s", var.name, result);
          }
          fb_send_info(allvar_result);
        } else {
          fb_send_fail(result);
        }

        // If we've exhausted all default args, or there are no default args,
        // move to the next var.
        curr_var_arg_idx += 1;
        if (!var.default_args || !var.default_args[curr_var_arg_idx]) {
          curr_var_idx += 1;
          curr_var_arg_idx = 0;
        }
      }

      break;
    }
  }
}

void respond_to_query_packet(fb_udp_pkt_t *pkt) {
  uint16_t be_seq_num = htons(expected_seq_num);
  memcpy((void *)pkt_to_send.data, (void *)&be_seq_num, sizeof(uint16_t));

  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, FB_UDP_HDR_SIZE + sizeof(uint16_t));
  }
  udp6_send_func((void *)&pkt_to_send, FB_UDP_HDR_SIZE + sizeof(uint16_t), dest_addr.daddr,
                 dest_addr.dport, dest_addr.sport);
}

void respond_to_init_packet(fb_udp_pkt_t *pkt) {
  // In this case, the response data is 2 big endian 2-byte values containing
  // the protocol version and max UDP packet size.
  max_pkt_size = sizeof(fb_udp_pkt_t);
  uint16_t data[2] = {htons(1), htons(max_pkt_size)};
  memcpy((void *)pkt_to_send.data, (void *)&data, 2 * sizeof(uint16_t));

  // Set the max packet size.
  uint16_t host_max_pkt_size = 0;
  memcpy((void *)&host_max_pkt_size, (void *)(pkt->data + 2), sizeof(uint16_t));
  if (ntohs(host_max_pkt_size) < max_pkt_size) {
    max_pkt_size = ntohs(host_max_pkt_size);
  }

  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, FB_UDP_HDR_SIZE + 4);
  }

  udp6_send_func((void *)&pkt_to_send, FB_UDP_HDR_SIZE + 4, dest_addr.daddr, dest_addr.dport,
                 dest_addr.sport);
}

void fb_reboot(char *cmd) {
  // Throw away the reboot command.
  strtok(cmd, "-");

  char *partition = strtok(NULL, "-");
  if (!partition) {
    set_bootbyte(gSys->RuntimeServices, EFI_BOOT_NORMAL);
  } else if (!strncmp(partition, "bootloader", 10)) {
    set_bootbyte(gSys->RuntimeServices, EFI_BOOT_BOOTLOADER);
  } else if (!strncmp(partition, "recovery", 8)) {
    set_bootbyte(gSys->RuntimeServices, EFI_BOOT_RECOVERY);
  }
  fb_send_okay("");

  // Set the reboot flag but don't do it right away so that we can complete
  // our TCP session to not leave the client hanging forever.
  fb_poll_action = REBOOT;
}

void fb_flash(char *cmd) {
  // Throw away the flash command string.
  strtok(cmd, ":");

  // Get the partition to flash by getting the next token.
  char *partition = strtok(NULL, ":");
  if (!partition) {
    fb_send_fail("no partition provided to flash");
    return;
  }

  const uint8_t *type_guid = partition_type_guid(partition);
  if (!type_guid) {
    fb_send_fail("could not find partition type GUID");
    return;
  }

  efi_status status = write_partition(gImg, gSys, type_guid, partition, PARTITION_OFFSET,
                                      (unsigned char *)curr_img.data, curr_img.size);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to write partition; efi_status: %016" PRIx64, status);
    fb_send_fail(err_msg);
    return;
  }

  fb_send_okay("");
}

void fb_erase(char *cmd) {
  // Throw away the erase command string.
  strtok(cmd, ":");

  // Get the partition to flash by getting the next token.
  char *partition = strtok(NULL, ":");
  if (!partition) {
    fb_send_fail("no partition provided to erase");
    return;
  }

  const uint8_t *type_guid = partition_type_guid(partition);
  if (!type_guid) {
    fb_send_fail("could not find partition type GUID");
    return;
  }

  disk_t disk;
  if (disk_find_boot(gImg, gSys, DEBUG, &disk) < 0) {
    fb_send_fail("could not find boot disk");
    return;
  }

  gpt_entry_t entry;
  if (disk_find_partition(&disk, DEBUG, type_guid, NULL, NULL, &entry)) {
    fb_send_fail("could not find partition");
    return;
  }
  uint64_t offset = entry.first * disk.blksz;
  uint64_t size = (entry.last - entry.first + 1) * disk.blksz;

  // Allocate some memory to clear.
  size_t num_pages = PAGE_SIZE * 16;
  efi_physical_addr pg_addr;
  efi_status status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, num_pages, &pg_addr);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to allocate memory; efi_status: %016" PRIx64, status);
    fb_send_fail(err_msg);
    return;
  }
  size_t increment = num_pages * PAGE_SIZE;
  memset((void *)pg_addr, 0xff, increment);

  // Clear the partition in 256MiB increments. This value is just large enough
  // to erase an entire zircon partition in less than 500ms. Admittedly, this
  // is a bit fragile to future partition size increases, so we should
  // probably intermittently poll the network interface so the host doesn't
  // think the port is closed.
  while (size > 0) {
    size_t len = (size < increment) ? size : increment;
    efi_status status = disk_write(&disk, offset, (void *)pg_addr, len);
    if (status != EFI_SUCCESS) {
      char err_msg[FB_CMD_MAX_LEN];
      snprintf(err_msg, FB_CMD_MAX_LEN, "failed to write to disk; efi_status: %016" PRIx64, status);
      fb_send_fail(err_msg);
      return;
    }
    size -= len;
    offset += len;
  }

  // Send the OKAY.
  fb_send_okay("");

  // Free the memory.
  gBS->FreePages(pg_addr, num_pages);
}

// Turns a hex string of exactly length 8 into a uint32_t.
uint32_t hex_to_int(const char *hexstring) {
  uint32_t value = 0;
  uint8_t hexstring_length = 8;
  uint8_t bits_per_char = 4;
  for (uint8_t i = 0; i < hexstring_length; i++) {
    char hex_digit = *(hexstring + i);
    uint32_t ascii = (uint32_t)hex_digit;
    if (ascii >= '0' && ascii <= '9') {
      // character is 0-9
      value += (ascii - '0') << ((7 - i) * bits_per_char);
    } else if (ascii >= 'a' && ascii <= 'f') {
      // character is a-f
      uint32_t intermediate = (ascii - 'a') + 10;
      value += intermediate << ((7 - i) * bits_per_char);
    } else {
      // This will lead to unexpected failures if the provided hexstring is
      // 0xffffffff, but this seems like a rare edge case.
      return -1;
    }
  }
  return value;
}

void fb_download(char *cmd) {
  // Throw away download command string.
  strtok(cmd, ":");

  // Free any pages used during a previous download.
  if (curr_img.data != NULL) {
    uint32_t pages_used = (curr_img.size + PAGE_SIZE - 1) / PAGE_SIZE;
    efi_status status = gBS->FreePages((efi_physical_addr)(curr_img.data), pages_used);
    if (status != EFI_SUCCESS) {
      char err_msg[FB_CMD_MAX_LEN];
      snprintf(err_msg, FB_CMD_MAX_LEN, "failed to free memory; efi_status: %016" PRIx64, status);
      fb_send_fail(err_msg);
      return;
    }
    curr_img.data = NULL;
    curr_img.bytes_received = 0;
  }

  // Get the size of the current download.
  char *hexstring = strtok(NULL, ":");
  if (!hexstring) {
    fb_send_fail("download size not provided");
    return;
  }
  curr_img.size = hex_to_int(hexstring);
  if (curr_img.size == (uint32_t)(-1)) {
    fb_send_fail("failed to convert download size to integer");
    return;
  }

  // Allocate space for the download.
  uint32_t pages_needed = (curr_img.size + PAGE_SIZE - 1) / PAGE_SIZE;
  efi_physical_addr mem_addr;
  efi_status status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages_needed, &mem_addr);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to allocate memory; efi_status: %016" PRIx64, status);
    fb_send_fail(err_msg);
    return;
  }
  curr_img.data = (void *)mem_addr;

  // Respond with the appropriate DATA packet.
  fb_send_data(hexstring);
  cmd_phase = DATA;
}

void fb_set_active(char *cmd) {
  // Throw away set-active command string.
  strtok(cmd, ":");

  char *slot = strtok(NULL, ":");
  if (!slot) {
    fb_send_fail("no slot provided to set-active");
    return;
  }

  AbrResult res;
  if (*slot == 'a') {
    res = zircon_abr_set_slot_active(kAbrSlotIndexA);
  } else if (*slot == 'b') {
    res = zircon_abr_set_slot_active(kAbrSlotIndexB);
  } else {
    fb_send_fail("invalid slot in set-active");
    return;
  }
  if (res != kAbrResultOk) {
    fb_send_fail("failed to set slot active");
    return;
  }

  fb_send_okay("");
}

// getvar retrieves the value of the requested fastboot variable (if it exists).
void fb_getvar(char *cmd) {
  // Throw away the "getvar" portion of the string.
  strtok(cmd, ":");

  char *varname = strtok(NULL, ":");
  if (!varname) {
    fb_send_fail("no variable provided");
    return;
  }

  char *arg = strtok(NULL, ":");

  bool found = false;
  for (int i = 0; i < NUM_VARIABLES; i++) {
    fb_var_t var = varlist[i];
    // strlen is safe here because all of the variable names and values
    // are constant strings specified above.
    if (!strncmp(varname, var.name, strlen(var.name))) {
      found = true;
      if (var.value != NULL) {
        fb_send_okay(var.value);
      } else {
        char result[FB_CMD_MAX_LEN];
        memset(result, 0, FB_CMD_MAX_LEN);  // Zero out to null terminate.
        if (var.func(arg, result) == 0) {
          fb_send_okay(result);
        } else {
          fb_send_fail(result);
        }
      }
      break;
    }
  }
  if (!found) {
    fb_send_fail("no such variable");
  }
}

// boots the previously downloaded image in memory.
void fb_boot(char *cmd) {
  uint32_t bootimg_hdr_version = validate_bootimg(curr_img.data);
  if (bootimg_hdr_version == (uint32_t)(-1)) {
    fb_send_fail("invalid boot image magic");
    return;
  }
  uint32_t kernel_size = get_kernel_size(curr_img.data, bootimg_hdr_version);
  if (kernel_size == (uint32_t)(-1)) {
    fb_send_fail("failed to get kernel size from bootimg");
    return;
  }
  uint32_t page_size = get_page_size(curr_img.data, bootimg_hdr_version);
  if (page_size == (uint32_t)(-1)) {
    fb_send_fail("failed to get page size from bootimg");
    return;
  }
  fb_poll_action = BOOT_FROM_RAM;
  boot_img.kernel_size = kernel_size;
  boot_img.kernel_start = curr_img.data + page_size;
  fb_send_okay("");
}

// resumes boot.
void fb_continue(char *cmd) {
  fb_poll_action = CONTINUE_BOOT;
  fb_send_okay("");
}

// stage a file to be added to the ZBI.
void fb_staged_bootloader_file(char *cmd) {
  // throw away "oem add-staged-bootloader-file"
  strtok(cmd, " ");
  strtok(NULL, " ");
  char *name = strtok(NULL, " ");
  if (!name) {
    fb_send_fail("No file name given");
    return;
  }

  zircon_stage_zbi_file(name, curr_img.data, curr_img.size);
  fb_send_okay("");
}

// get_max_download_size puts the size of the largest contiguous section of
// memory in the result buffer. Returns 0 on success, -1 on failure.
int get_max_download_size(const char *arg, char *result) {
  efi_memory_type mem_type = EfiLoaderData | EfiConventionalMemory;
  uint64_t max_download_size = 0;
  // Get memory map.
  static char buf[32786];
  size_t buf_size = sizeof(buf);
  size_t mkey = 0;
  size_t dsize = 0;
  uint32_t dversion = 0;
  efi_status status =
      gBS->GetMemoryMap(&buf_size, (efi_memory_descriptor *)buf, &mkey, &dsize, &dversion);
  if (status != EFI_SUCCESS) {
    snprintf(result, FB_CMD_MAX_LEN, "failed to get memory map; efi_status: %016" PRIx64, status);
    return -1;
  }
  // Look through the memory map for the largest contiguous region of memory.
  for (void *p = (void *)buf; p < (void *)(buf) + buf_size; p += dsize) {
    efi_memory_descriptor *des = (efi_memory_descriptor *)p;
    if ((des->Type & mem_type) && (des->NumberOfPages * PAGE_SIZE) > max_download_size) {
      max_download_size = (des->NumberOfPages * PAGE_SIZE);
    }
  }
  snprintf(result, FB_CMD_MAX_LEN, "0x%016" PRIx64, max_download_size);
  return 0;
}

// get_current_slot returns the current boot slot.
int get_current_slot(const char *arg, char *result) {
  AbrSlotIndex idx = zircon_abr_get_boot_slot(false);
  switch (idx) {
    case kAbrSlotIndexA:
      strncpy(result, "a", FB_CMD_MAX_LEN);
      break;
    case kAbrSlotIndexB:
      strncpy(result, "b", FB_CMD_MAX_LEN);
      break;
    case kAbrSlotIndexR:
      strncpy(result, "r", FB_CMD_MAX_LEN);
      break;
    default:
      strncpy(result, "failed to get boot slot", FB_CMD_MAX_LEN);
      return -1;
  }
  return 0;
}

// get_slot_info is a helper function that populates an AbrSlotInfo object given
// a slot.
// Returns 0 on success, -1 on failure.
int get_slot_info(char slot, AbrSlotInfo *info) {
  AbrSlotIndex slotIdx;
  if (slot == 'a') {
    slotIdx = kAbrSlotIndexA;
  } else if (slot == 'b') {
    slotIdx = kAbrSlotIndexB;
  } else {
    // Fastboot does not support getting boot bit for any other partition.
    return -1;
  }
  if (zircon_abr_get_slot_info(slotIdx, info) != kAbrResultOk) {
    return -1;
  }
  return 0;
}

int get_slot_unbootable(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_CMD_MAX_LEN);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_CMD_MAX_LEN);
    return -1;
  }

  if (!info.is_bootable) {
    strncpy(result, "yes", FB_CMD_MAX_LEN);
  } else {
    strncpy(result, "no", FB_CMD_MAX_LEN);
  }
  return 0;
}

int get_slot_successful(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_CMD_MAX_LEN);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_CMD_MAX_LEN);
    return -1;
  }

  if (info.is_marked_successful) {
    strncpy(result, "yes", FB_CMD_MAX_LEN);
  } else {
    strncpy(result, "no", FB_CMD_MAX_LEN);
  }
  return 0;
}

int get_slot_retry_count(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_CMD_MAX_LEN);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_CMD_MAX_LEN);
    return -1;
  }

  snprintf(result, FB_CMD_MAX_LEN, "%d", (kAbrMaxTriesRemaining - info.num_tries_remaining));
  return 0;
}

void pp_fb_pkt(const char *direction, fb_udp_pkt_t *pkt, size_t len) {
  // Pretty printing is too slow when transferring data, so skip in the data
  // phase. TCP dump is generally sufficient when debugging data transfer
  // issues.
  if (cmd_phase == DATA) {
    return;
  }
  printf("Size: %zu, %s: ", len, direction);
  switch (pkt->pkt_id) {
    case ERROR_TYPE:
      printf("ERROR");
      break;
    case QUERY_TYPE:
      printf("QUERY");
      break;
    case INIT_TYPE:
      printf("INIT");
      printf("    Protocol version: 0x%04x ", *((uint16_t *)(pkt->data)));
      printf("    Max packet size: 0x%04x", *((uint16_t *)(pkt->data) + 1));
      break;
    case FASTBOOT_TYPE:
      printf("FASTBOOT");
      break;
    default:
      printf("error: malformed type: %#02x", pkt->pkt_id);
      return;
  }
  printf("    Flags: %02x", pkt->pkt_flags);
  printf("    Seq_Num: %04x", pkt->seq_num);
  pkt->data[len - FB_UDP_HDR_SIZE] = '\0';
  printf("    Data: \"%s\" \n", pkt->data);
}

void fb_send_ack(void) {
  pkt_to_send_len = FB_UDP_HDR_SIZE;
  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, pkt_to_send_len);
  }
  udp6_send_func((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
                 dest_addr.sport);
}

void fb_resend(void) {
  udp6_send_func((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
                 dest_addr.sport);
}

void fb_send(const char *type, const char *msg) {
  // Truncate the message if necessary, reserving the first 4 bytes for the
  // message type (OKAY/FAIL/etc).
  size_t msg_len = strlen(msg);
  if (msg_len > FB_CMD_MAX_LEN - 4) {
    WLOG("FB message too long, truncating (full: '%s')", msg);
    msg_len = FB_CMD_MAX_LEN - 4;
  }

  if (fb_tcp_state & TCP_STATE_ACTIVE_FLAG) {
    // TCP format: 8 bytes network-order length + 4 byte type + packet.
    uint64_t network_order_msg_len = htonll(msg_len + 4);
    memcpy(fb_tcp_buffer, &network_order_msg_len, 8);
    memcpy(fb_tcp_buffer + 8, type, 4);
    memcpy(fb_tcp_buffer + 12, msg, msg_len);
    fb_tcp_length = (uint32_t)(msg_len + 12);
    fb_tcp_state = TCP_STATE_WRITE;
  } else {
    memcpy(pkt_to_send.data, type, 4);
    memcpy(pkt_to_send.data + 4, msg, msg_len);
    // Some of our UDP logic expects a trailing \0 for convenience.
    pkt_to_send.data[msg_len + 4] = '\0';
    pkt_to_send_len = FB_UDP_HDR_SIZE + msg_len + 4;
    // Send the packet.
    if (DEBUG) {
      pp_fb_pkt("device", &pkt_to_send, pkt_to_send_len);
    }
    udp6_send_func((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
                   dest_addr.sport);
  }
}

void fb_send_okay(const char *msg) { fb_send("OKAY", msg); }
void fb_send_fail(const char *msg) { fb_send("FAIL", msg); }
void fb_send_data(const char *msg) { fb_send("DATA", msg); }
void fb_send_info(const char *msg) { fb_send("INFO", msg); }
