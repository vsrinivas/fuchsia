// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/integration_test/test_serial.h"

#include <iostream>
#include <sstream>

#include <lib/fxl/logging.h>
#include <lib/zx/time.h>

static constexpr bool kGuestOutput = false;
static constexpr size_t kSerialBufferSize = 1024;
static constexpr zx::duration kTestTimeout = zx::sec(5);

static std::string command_hash(const std::string& command) {
  size_t hash = std::hash<std::string>{}(command);
  std::stringstream hash_ss;
  hash_ss << hash;

  return hash_ss.str();
}

// Sends a command and waits for the response. We capture output by echoing a
// header and footer before and after the command. Then we wait for the command
// to be written back to the serial, then the header, then finally we capture
// everything until the footer.
zx_status_t TestSerial::ExecuteBlocking(const std::string& command,
                                        std::string* result) {
  std::string header = command_hash(command);
  std::string footer = header;
  std::reverse(footer.begin(), footer.end());

  std::string full_command =
      "echo " + header + "; " + command + "; echo " + footer;
  zx_status_t status = SendBlocking(full_command + "\n");
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to send command: " << status;
    return status;
  }

  status = WaitForMarker(full_command);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command echo: " << status;
    return status;
  }

  status = WaitForMarker(header);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command header: " << status;
    return status;
  }

  status = WaitForMarker(footer, result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command footer: " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t TestSerial::SendBlocking(const std::string& message) {
  zx_status_t status;
  const char* data = message.data();
  size_t len = message.size();
  do {
    zx_signals_t pending = 0;
    status = socket_.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                              zx::deadline_after(kTestTimeout), &pending);
    if (status != ZX_OK) {
      return status;
    }
    if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }
    if (!(pending & ZX_SOCKET_WRITABLE)) {
      continue;
    }
    size_t actual;
    status = zx_socket_write(socket_.get(), 0, data, len, &actual);
    if (status == ZX_OK && actual < len) {
      data += actual;
      len -= actual;
      status = ZX_ERR_SHOULD_WAIT;
    }
  } while (status == ZX_ERR_SHOULD_WAIT);

  return status;
}

zx_status_t TestSerial::WaitForMarker(const std::string& marker,
                                      std::string* result) {
  std::string output = buffer_;
  buffer_.erase();
  zx_status_t status;
  do {
    auto marker_loc = output.rfind(marker);
    if (marker_loc != std::string::npos && !output.empty()) {
      // Do not accept a marker that isn't terminated by a newline.
      if (marker_loc + marker.size() != output.size() &&
          output[marker_loc + marker.size()] == '\n') {
        // If we have read the socket past the end of the marker, make sure
        // what's left is kept in the buffer for the next read.
        if (marker_loc + marker.size() + 1 < output.size()) {
          buffer_ = output.substr(marker_loc + marker.size() + 1);
        }
        if (result == nullptr) {
          return ZX_OK;
        }
        output.erase(marker_loc);
        *result = output;
        return ZX_OK;
      }
    }

    zx_signals_t pending = 0;
    status = socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                              zx::deadline_after(kTestTimeout), &pending);
    if (status != ZX_OK) {
      return status;
    } else if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }
    char buf[kSerialBufferSize];
    size_t actual;
    status = zx_socket_read(socket_.get(), 0, buf, sizeof(buf), &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      continue;
    } else if (status != ZX_OK) {
      return status;
    }
    if (kGuestOutput) {
      std::cout.write(buf, actual);
      std::cout.flush();
    }
    output.append(buf, actual);
  } while (status == ZX_ERR_SHOULD_WAIT || status == ZX_OK);
  return status;
}
