// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_EVENTS_H_
#define APPS_NETSTACK_EVENTS_H_

#define EVENT_NONE 0
#define EVENT_READ 1
#define EVENT_WRITE 2
#define EVENT_EXCEPT 4

#define EVENT_ALL (EVENT_READ | EVENT_WRITE | EVENT_EXCEPT)

void fd_event_set(int sockfd, int events);
void fd_event_clear(int sockfd, int events);

#endif  // APPS_NETSTACK_EVENTS_H_
