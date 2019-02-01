// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MIME_SNIFFER_MIME_SNIFFER_H_
#define GARNET_LIB_MIME_SNIFFER_MIME_SNIFFER_H_

#include <stdint.h>
#include <string>

// The content was this file and mime_sniffer.cc were copied from
// https://chromium.googlesource.com/chromium/src/net/+/master/base/mime_sniffer.h
// and its counterpart. Only the code which can be used to sniff Htnml was
// copied. Minimum modifications were done so that we can use this with fuchsia.
// If we decide to port whole code we should also port unittests.

namespace mime_sniffer {

// The maximum number of bytes used by any internal mime sniffing routine.
//
// This must be updated if any internal sniffing routine needs more bytes.
const int kMaxBytesToSniff = 1024;

bool SniffForHTML(const char* content, size_t size, bool* have_enough_content,
                  std::string* result);

}  // namespace mime_sniffer

#endif  // GARNET_LIB_MIME_SNIFFER_MIME_SNIFFER_H_
