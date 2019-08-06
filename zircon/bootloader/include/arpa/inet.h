// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_ARPA_INET_H_
#define ZIRCON_BOOTLOADER_INCLUDE_ARPA_INET_H_

#include <stdint.h>

uint16_t htons(uint16_t val);
uint16_t ntohs(uint16_t val);
uint32_t htonl(uint32_t val);
uint32_t ntohl(uint32_t val);

#endif  // ZIRCON_BOOTLOADER_INCLUDE_ARPA_INET_H_
