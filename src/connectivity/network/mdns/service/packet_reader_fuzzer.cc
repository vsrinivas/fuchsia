// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "dns_reading.h"
#include "packet_reader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  mdns::PacketReader reader(std::vector<uint8_t>(Data, Data + Size));
  reader.SetBytesRemaining(Size);
  auto message = std::make_unique<mdns::DnsMessage>();
  reader >> *message.get();

  return 0;
}
