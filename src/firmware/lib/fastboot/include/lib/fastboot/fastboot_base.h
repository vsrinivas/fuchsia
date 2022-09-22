// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_BASE_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_BASE_H_

#include <lib/zx/status.h>
#include <stddef.h>

#include <string_view>

namespace fastboot {
// Communication between the fastboot host and device is in the unit of
// "packet". Each fastboot command and response message (INFO, OKAY, FAIL,
// DATA) is a single packet. In the DATA phase, the data to download or upload
// is sent via one or more packets.
//
// Fastboot USB and TCP use different mechanisms in delivering a "packet".
// For USB transport, each USB request is a single packet. Communication
// is usually driven by callback/interrupt. For TCP stream, packets are
// organized as length-prefixed bytes sequence, i.e.:
//
//   <length><payload><length><payload>...
//
// Fastboot TCP additionally has a handshake phase at the start of a TCP
// session, where both sides expect and exchange a 4-byte message
// "FB<2-digit version number>" i.e. "FB01", before starting the bytes
// stream.
//
// To simplify the design for this device-side library, we draw the boundary
// at only providing class/APIs to process a single fastboot packet at a time.
// Users are responsible for handling transport level details, including
// extracting/passing packet and providing method for sending packets. This is
// done by implementing the `class Transport` abstract class below, and
// passing to the API `Fastboot::ProcessPacket(...)`;

class Transport {
 public:
  virtual ~Transport() = default;

  // Fetch a packet in to a given buffer of a given capacity.
  // Implementation should check against the given capacity. It should block
  // until the entire packet is read into the given buffer.
  virtual zx::status<size_t> ReceivePacket(void *dst, size_t capacity) = 0;

  // Peek the size of the next packet.
  virtual size_t PeekPacketSize() = 0;

  // Send a packet over the transport.
  // Note: Once the method returns, implementation should not assume the
  // memory backing `packet` is still valid. In the case of `fastboot reboot`
  // The system might event start power cycle shortly after the method returns.
  // Thus implementation should block at least until the packet is sent out to the
  // transport.
  virtual zx::status<> Send(std::string_view packet) = 0;
};

constexpr char kOemPrefix[] = "oem ";

// Host packet size max is 64.
constexpr size_t kMaxCommandPacketSize = 64;
// Argument are separated by either ":" or " "
constexpr size_t kMaxCommandArgs = kMaxCommandPacketSize / 2;

// An abstract base class for fastboot. It provides built in support for `fastboot download`
// command. Implementation provide the download buffer by implementing GetDownloadBuffer() method.
// It also provides a few utility helpers such as sending fastboot response and matching/parsing
// fastboot commands. Other commands are implemented by overriding the ProcessCommand() method.
class FastbootBase {
 public:
  enum class State {
    kCommand,
    kDownload,
  };

  zx::status<> ProcessPacket(Transport *transport);

  // Get the total and remaining download size when running `fastboot download` command.
  size_t total_download_size() const { return total_download_size_; }
  size_t remaining_download_size() const { return remaining_download_size_; }
  State state() { return state_; }

  // Match an in-coming command `cmd` with a reference command `ref`
  static bool MatchCommand(std::string_view cmd, std::string_view ref);

  struct CommandArgs {
    std::string_view args[kMaxCommandArgs];
    size_t num_args;
  };

  // A helper to extract command arguments.
  static void ExtractCommandArgs(std::string_view cmd, const char *delimeter, CommandArgs &ret);

  enum class ResponseType {
    kOkay,
    kInfo,
    kFail,
  };

  // A helper to send fastboot response message of type OKAY, INFO, and FAIL. Caller can specify
  // a failure code in `status_code` to add to the message. `status_code` will be merged into the
  // return status. Specifically, if response is successfully sent without error, `status_code` is
  // returned. Otherwise the error code while sending the message is returned. This is to make it
  // easier for callsite so that it doesn't have to check multiple error codes and decide which to
  // returns.
  static zx::status<> SendResponse(ResponseType resp_type, std::string_view message,
                                   Transport *transport, zx::status<> status_code = zx::ok());

  // Send a data response package "DATA<hex string of `datasize`>" i.e. DATA0x12345678
  static zx::status<> SendDataResponse(size_t data_size, Transport *transport);

 private:
  // Prepare a buffer for storing download buffer
  virtual zx::status<void *> GetDownloadBuffer(size_t total_download_size) = 0;

  // Process a command. Implementation is responsible for handling all needed response to sent.
  virtual zx::status<> ProcessCommand(std::string_view cmd, Transport *transport) = 0;

  // Perform implementation-specific clearing/resetting of download, i.e releasing buffer.
  virtual void DoClearDownload() = 0;

  void ClearDownload();
  zx::status<> Download(std::string_view cmd, Transport *transport);

  State state_ = State::kCommand;
  size_t remaining_download_size_ = 0;
  size_t total_download_size_ = 0;
  void *download_buffer_ = nullptr;
};

}  // namespace fastboot

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_BASE_H_
