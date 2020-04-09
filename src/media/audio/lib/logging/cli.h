// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_LOGGING_CLI_H_
#define SRC_MEDIA_AUDIO_LIB_LOGGING_CLI_H_

#include <zircon/status.h>

#include <iostream>

#define CLI_CHECK(TEST, MSG)                                                               \
  do {                                                                                     \
    if (!(TEST)) {                                                                         \
      std::cerr << std::endl << __FILE__ << " (" << __LINE__ << "): " << MSG << std::endl; \
      std::exit(1);                                                                        \
    }                                                                                      \
  } while (0)

#define CLI_CHECK_OK(STATUS, MSG)                                                      \
  do {                                                                                 \
    if (STATUS != ZX_OK) {                                                             \
      std::cerr << std::endl                                                           \
                << __FILE__ << " (" << __LINE__ << ") " << MSG << ": "                 \
                << zx_status_get_string(STATUS) << " (" << STATUS << ")" << std::endl; \
      std::exit(1);                                                                    \
    }                                                                                  \
  } while (0)

#endif  // SRC_MEDIA_AUDIO_LIB_LOGGING_CLI_H_
