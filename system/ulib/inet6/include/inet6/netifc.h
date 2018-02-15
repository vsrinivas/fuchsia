// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// setup networking
// if interface != NULL, only use the given topological path for networking
int netifc_open(const char* interface);

// process inbound packet(s)
int netifc_poll(void);

// return nonzero if interface exists
int netifc_active(void);

// shut down networking
void netifc_close(void);

// set a timer to expire after ms milliseconds
void netifc_set_timer(uint32_t ms);

// returns true once the timer has expired
int netifc_timer_expired(void);

void netifc_recv(void* data, size_t len);

// send out next pending packet, and return value indicating if more are available to send
bool netifc_send_pending(void);

void netifc_get_info(uint8_t* addr, uint16_t* mtu);
