// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/socket/strings.h"

#include <utility>

#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/socket/blocking_drain.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

bool BlockingCopyToString(zx::socket source, std::string* result) {
  LEDGER_CHECK(result);
  result->clear();
  return BlockingDrainFrom(std::move(source), [result](const void* buffer, uint32_t num_bytes) {
    result->append(static_cast<const char*>(buffer), num_bytes);
    return num_bytes;
  });
}

bool BlockingCopyFromString(absl::string_view source, const zx::socket& destination) {
  const char* ptr = source.data();
  size_t to_write = source.size();
  for (;;) {
    size_t written;
    zx_status_t result = destination.write(0, ptr, to_write, &written);
    if (result == ZX_OK) {
      if (written == to_write)
        return true;
      to_write -= written;
      ptr += written;
    } else if (result == ZX_ERR_SHOULD_WAIT) {
      result = destination.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                    zx::time::infinite(), nullptr);
      if (result != ZX_OK) {
        // If the socket was closed, then treat as EOF.
        return result == ZX_ERR_PEER_CLOSED;
      }
    } else {
      // If the socket was closed, then treat as EOF.
      return result == ZX_ERR_PEER_CLOSED;
    }
  }
}

zx::socket WriteStringToSocket(absl::string_view source) {
  // TODO(qsr): Check that source.size() <= socket max capacity when the
  // information is retrievable. Until then use the know socket capacity.
  LEDGER_DCHECK(source.size() < 256 * 1024);
  zx::socket socket1, socket2;
  zx::socket::create(0u, &socket1, &socket2);
  BlockingCopyFromString(source, std::move(socket1));
  return socket2;
}

}  // namespace ledger
