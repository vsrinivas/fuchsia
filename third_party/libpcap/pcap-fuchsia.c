// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pcap-fuchsia.h"

pcap_t *pcap_create_common_fuchsia(char *ebuf) {
  return PCAP_CREATE_COMMON(ebuf, struct pcap_fuchsia);
}
