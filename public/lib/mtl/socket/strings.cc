// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/strings.h"

#include <utility>

#include "lib/fxl/logging.h"
#include "lib/mtl/socket/blocking_drain.h"

namespace mtl {

bool BlockingCopyToString(mx::socket source, std::string* result) {
  FXL_CHECK(result);
  result->clear();
  return BlockingDrainFrom(
      std::move(source), [result](const void* buffer, uint32_t num_bytes) {
        result->append(static_cast<const char*>(buffer), num_bytes);
        return num_bytes;
      });
}

bool BlockingCopyFromString(fxl::StringView source,
                            const mx::socket& destination) {
  const char* ptr = source.data();
  size_t to_write = source.size();
  for (;;) {
    size_t written;
    mx_status_t result = destination.write(0, ptr, to_write, &written);
    if (result == MX_OK) {
      if (written == to_write)
        return true;
      to_write -= written;
      ptr += written;
    } else if (result == MX_ERR_SHOULD_WAIT) {
      result = destination.wait_one(MX_SOCKET_WRITABLE | MX_SOCKET_PEER_CLOSED,
                                    MX_TIME_INFINITE, nullptr);
      if (result != MX_OK) {
        // If the socket was closed, then treat as EOF.
        return result == MX_ERR_PEER_CLOSED;
      }
    } else {
      // If the socket was closed, then treat as EOF.
      return result == MX_ERR_PEER_CLOSED;
    }
  }
}

mx::socket WriteStringToSocket(fxl::StringView source) {
  // TODO(qsr): Check that source.size() <= socket max capacity when the
  // information is retrievable. Until then use the know socket capacity.
  FXL_DCHECK(source.size() < 256 * 1024);
  mx::socket socket1, socket2;
  mx::socket::create(0u, &socket1, &socket2);
  BlockingCopyFromString(source, std::move(socket1));
  return socket2;
}

}  // namespace mtl
