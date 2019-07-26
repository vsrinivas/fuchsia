// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <xdc-server-utils/stream.h>

// Stream id used to send control messages between the host and debug device.
#define XDC_MSG_STREAM DEBUG_STREAM_ID_RESERVED

// Control message opcodes used in xdc_msg_t.
#define XDC_NOTIFY_STREAM_STATE 0x01

typedef struct {
  uint32_t stream_id;
  bool online;
} xdc_notify_stream_state_t;

// Messages sent over the XDC_MSG_STREAM.
typedef struct {
  uint8_t opcode;
  union {
    xdc_notify_stream_state_t notify_stream_state;
  };
} xdc_msg_t;
