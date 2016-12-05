// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/strings.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "lib/mtl/socket/blocking_drain.h"

namespace mtl {

bool BlockingCopyToString(mx::socket source, std::string* result) {
  FTL_CHECK(result);
  result->clear();
  return BlockingDrainFrom(
      std::move(source), [result](const void* buffer, uint32_t num_bytes) {
        result->append(static_cast<const char*>(buffer), num_bytes);
        return num_bytes;
      });
}

bool BlockingCopyFromString(ftl::StringView source,
                            const mx::socket& destination) {
  const char* ptr = source.data();
  size_t to_write = source.size();
  for (;;) {
    mx_size_t written;
    mx_status_t result = destination.write(0, ptr, to_write, &written);
    if (result == NO_ERROR) {
      if (written == to_write)
        return true;
      to_write -= written;
      ptr += written;
    } else if (result == ERR_SHOULD_WAIT) {
      result = destination.wait_one(MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                    MX_TIME_INFINITE, nullptr);
      if (result != NO_ERROR) {
        // If the socket was closed, then treat as EOF.
        return result == ERR_REMOTE_CLOSED;
      }
    } else {
      // If the socket was closed, then treat as EOF.
      return result == ERR_REMOTE_CLOSED;
    }
  }
}

mx::socket WriteStringToSocket(ftl::StringView source) {
  // TODO(qsr): Check that source.size() <= socket max capacity when the
  // information is retrievable. Until then use the know socket capacity.
  FTL_DCHECK(source.size() < 256 * 1024);
  mx::socket socket1, socket2;
  mx::socket::create(0u, &socket1, &socket2);
  BlockingCopyFromString(source, std::move(socket1));
  return socket2;
}

}  // namespace mtl
