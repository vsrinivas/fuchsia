// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/integration/test_serial.h"

#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/time.h>
#include <iostream>
#include <regex>

#include "logger.h"

static constexpr size_t kSerialBufferSize = 1024;
static constexpr zx::duration kTestTimeout = zx::sec(15);
static constexpr zx::duration kSerialStableDelay = zx::msec(800);

// This is the maximum line length of dash in both zircon_guest and
// debian_guest.
static constexpr size_t kMaximumLineLength = 4096;

static std::string command_hash(const std::string& command) {
  std::hash<std::string> hash;
  return fxl::StringPrintf("%zu", hash(command));
}

zx_status_t TestSerial::Start(zx::socket socket) {
  socket_ = std::move(socket);

  // Wait for something to be sent over serial. Both Zircon and Debian will send
  // at least a command prompt. For Debian, this is necessary since any commands
  // we send will be ignored until the guest is ready.
  zx_status_t status = WaitForAny();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start serial";
  }

  // Wait for output to stabilize
  zx_signals_t pending = 0;
  do {
    zx_status_t status =
        socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                         zx::deadline_after(kSerialStableDelay), &pending);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      FXL_LOG(ERROR) << "Error waiting for socket " << status;
      return status;
    }
    Drain();
  } while (pending & ZX_SOCKET_READABLE);

  return status;
}

// Sends a command and waits for the response. We capture output by echoing a
// header and footer before and after the command. Then we wait for the command
// to be written back to the serial, then the header, then finally we capture
// everything until the footer.
zx_status_t TestSerial::ExecuteBlocking(const std::string& command,
                                        const std::string& prompt,
                                        std::string* result) {
  std::string header = command_hash(command);
  std::string footer = header;
  std::reverse(footer.begin(), footer.end());

  std::string full_command =
      "echo " + header + "; " + command + "; echo " + footer;
  if (full_command.size() > kMaximumLineLength) {
    FXL_LOG(ERROR) << "Command is too long";
    return ZX_ERR_OUT_OF_RANGE;
  }

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

  status = WaitForMarker(header + "\n");
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command header: " << status;
    return status;
  }

  status = WaitForMarker(footer + "\n", result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command footer: " << status;
    return status;
  }

  status = WaitForMarker(prompt);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for command prompt: " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t TestSerial::SendBlocking(const std::string& message) {
  zx_status_t status;
  const char* data = message.data();
  size_t len = message.size();
  while (true) {
    zx_signals_t pending = 0;
    status = socket_.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                              zx::deadline_after(kTestTimeout), &pending);
    if (status != ZX_OK) {
      return status;
    } else if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    } else if (!(pending & ZX_SOCKET_WRITABLE)) {
      continue;
    }
    size_t actual;
    status = socket_.write(0, data, len, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      continue;
    } else if (status != ZX_OK) {
      return status;
    }
    if (actual == len) {
      return ZX_OK;
    }
    data += actual;
    len -= actual;
  }
}

zx_status_t TestSerial::WaitForMarker(const std::string& marker,
                                      std::string* result) {
  std::string output = buffer_;
  buffer_.erase();
  zx_status_t status;
  while (true) {
    auto marker_loc = output.rfind(marker);
    if (marker_loc != std::string::npos && !output.empty()) {
      // If we have read the socket past the end of the marker, make sure
      // what's left is kept in the buffer for the next read.
      if (marker_loc + marker.size() < output.size()) {
        buffer_ = output.substr(marker_loc + marker.size());
      }
      if (result == nullptr) {
        return ZX_OK;
      }
      output.erase(marker_loc);
      *result = output;
      return ZX_OK;
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
    status = socket_.read(0, buf, sizeof(buf), &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      continue;
    } else if (status != ZX_OK) {
      return status;
    }
    Logger::Get().Write(buf, actual);
    // Strip carriage returns to normalise both guests on newlines only.
    for (size_t i = 0; i != actual; ++i) {
      if (buf[i] == '\r') {
        continue;
      }
      output.push_back(buf[i]);
    }
  }
  return status;
}

zx_status_t TestSerial::Drain() {
  while (true) {
    char buf[kSerialBufferSize];
    size_t actual;
    zx_status_t status = socket_.read(0, buf, sizeof(buf), &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ZX_ERR_SHOULD_WAIT;
    }
    if (status != ZX_OK) {
      return status;
    }
    Logger::Get().Write(buf, actual);
  }
}

// Waits for something to be written to the socket and drains it.
zx_status_t TestSerial::WaitForAny() {
  zx_status_t status;
  zx_signals_t pending = 0;
  status = socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                            zx::deadline_after(kTestTimeout), &pending);
  if (status != ZX_OK) {
    return status;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    return ZX_ERR_PEER_CLOSED;
  }
  status = Drain();
  return status == ZX_ERR_SHOULD_WAIT ? ZX_OK : status;
}
