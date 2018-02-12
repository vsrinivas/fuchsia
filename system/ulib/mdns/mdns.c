// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mdns/mdns.h>

#include <string.h>


void mdns_init_message(mdns_message* m) {
  memset(m, 0, sizeof(mdns_message));
}

