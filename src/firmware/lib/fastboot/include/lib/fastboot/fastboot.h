// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/status.h>
#include <stddef.h>
#include <zircon/status.h>

#include <string>
#include <string_view>
#include <unordered_map>

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
  // Implementation should block at least until the packet is sent to
  // transport or copied. Once the method returns, implementation should not
  // assume the memory backing `packet` is still valid.
  virtual zx::status<> Send(std::string_view packet) = 0;
};

class __EXPORT Fastboot {
 public:
  explicit Fastboot(size_t max_download_size);
  // For test svc_root injection
  Fastboot(size_t max_download_size, fidl::ClientEnd<fuchsia_io::Directory> svc_root);
  zx::status<> ProcessPacket(Transport *transport);

 private:
  enum class State {
    kCommand,
    kDownload,
  };

  State state_ = State::kCommand;
  size_t max_download_size_;
  size_t remaining_download_ = 0;
  fzl::OwnedVmoMapper download_vmo_mapper_;
  // Channel to svc.
  fidl::ClientEnd<fuchsia_io::Directory> svc_root_;

  zx::status<> GetVar(const std::string &command, Transport *transport);
  zx::status<std::string> GetVarMaxDownloadSize(const std::vector<std::string_view> &, Transport *);
  zx::status<> Download(const std::string &command, Transport *transport);
  zx::status<> Flash(const std::string &command, Transport *transport);
  zx::status<> SetActive(const std::string &command, Transport *transport);

  void ClearDownload();
  zx::status<fidl::WireSyncClient<fuchsia_paver::Paver>> ConnectToPaver();
  zx::status<> WriteFirmware(fuchsia_paver::wire::Configuration config,
                             std::string_view firmware_type, Transport *transport,
                             fidl::WireSyncClient<fuchsia_paver::DataSink> &data_sink);
  zx::status<> WriteAsset(fuchsia_paver::wire::Configuration config,
                          fuchsia_paver::wire::Asset asset, Transport *transport,
                          fidl::WireSyncClient<fuchsia_paver::DataSink> &data_sink);

  struct CommandEntry {
    const char *name;
    zx::status<> (Fastboot::*cmd)(const std::string &, Transport *);
  };

  using VariableHashTable =
      std::unordered_map<std::string, zx::status<std::string> (Fastboot::*)(
                                          const std::vector<std::string_view> &, Transport *)>;

  // A static table of command name to method mapping.
  static const std::vector<CommandEntry> &GetCommandTable();

  // A static table of fastboot variable name to method mapping.
  static const VariableHashTable &GetVariableTable();

  friend class FastbootDownloadTest;
};

}  // namespace fastboot

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_FASTBOOT_H_
