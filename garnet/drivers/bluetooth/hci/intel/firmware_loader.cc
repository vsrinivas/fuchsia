// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "firmware_loader.h"
#include "logging.h"

#include <endian.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <limits>

#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <zircon/status.h>

namespace btintel {

using ::btlib::common::BufferView;
using ::btlib::common::PacketView;

FirmwareLoader::LoadStatus FirmwareLoader::LoadBseq(const void* firmware,
                                                    const size_t& len) {
  BufferView file(firmware, len);

  size_t offset = 0;
  bool patched = false;

  if (file.size() < sizeof(btlib::hci::CommandHeader)) {
    errorf("FirmwareLoader: Error: BSEQ too small: %zu < %zu\n", len,
           sizeof(btlib::hci::CommandHeader));
    return LoadStatus::kError;
  }

  // A bseq file consists of a sequence of:
  // - [0x01] [command w/params]
  // - [0x02] [expected event w/params]
  while (file.size() - offset > sizeof(btlib::hci::CommandHeader)) {
    // Parse the next items
    if (file[offset] != 0x01) {
      errorf("FirmwareLoader: Error: expected command packet\n");
      return LoadStatus::kError;
    }
    offset++;
    BufferView command_view = file.view(offset);
    PacketView<btlib::hci::CommandHeader> command(&command_view);
    command = PacketView<btlib::hci::CommandHeader>(
        &command_view, command.header().parameter_total_size);
    offset += command.size();
    if (!patched && le16toh(command.header().opcode) == kLoadPatch) {
      patched = true;
    }
    if ((file.size() - offset <= sizeof(btlib::hci::EventHeader)) ||
        (file[offset] != 0x02)) {
      errorf("FirmwareLoader: Error: expected event packet\n");
      return LoadStatus::kError;
    }
    std::deque<BufferView> events;
    while ((file.size() - offset > sizeof(btlib::hci::EventHeader)) &&
           (file[offset] == 0x02)) {
      offset++;
      BufferView event_view = file.view(offset);
      PacketView<btlib::hci::EventHeader> event(&event_view);
      size_t event_size =
          sizeof(btlib::hci::EventHeader) + event.header().parameter_total_size;
      events.emplace_back(file.view(offset, event_size));
      offset += event_size;
    }

    if (!hci_cmd_.SendAndExpect(command, std::move(events))) {
      return LoadStatus::kError;
    }
  }

  return patched ? LoadStatus::kPatched : LoadStatus::kComplete;
}

FirmwareLoader::LoadStatus FirmwareLoader::LoadSfi(const void* firmware,
                                                   const size_t& len) {
  BufferView file(firmware, len);

  if (file.size() < 644) {
    errorf("FirmwareLoader: SFI is too small: %zu < 644\n", file.size());
    return LoadStatus::kError;
  }

  size_t offset = 0;
  // SFI File format:
  // [128 bytes CSS Header]
  if (!hci_acl_.SendSecureSend(0x00, file.view(offset, 128))) {
    errorf("FirmwareLoader: Failed sending CSS Header!\n");
    return LoadStatus::kError;
  }
  offset += 128;
  // [256 bytes PKI]
  if (!hci_acl_.SendSecureSend(0x03, file.view(offset, 256))) {
    errorf("FirmwareLoader: Failed sending PKI Header!\n");
    return LoadStatus::kError;
  }
  offset += 256;
  // There are 4 bytes of unknown data here, that need to be skipped
  // for the file format to be correct later (command sequences)
  offset += 4;
  // [256 bytes signature info]
  if (!hci_acl_.SendSecureSend(0x02, file.view(offset, 256))) {
    errorf("FirmwareLoader: Failed sending signature Header!\n");
    return LoadStatus::kError;
  }
  offset += 256;

  size_t frag_len = 0;
  // [N bytes of command packets, arranged so that the "Secure send" command
  // param size can be a multiple of 4 bytes]
  while (offset < file.size()) {
    auto next_cmd = file.view(offset + frag_len);
    PacketView<btlib::hci::CommandHeader> header(&next_cmd);
    size_t cmd_size = sizeof(btlib::hci::CommandHeader) +
                      header.header().parameter_total_size;
    frag_len += cmd_size;
    if ((frag_len % 4) == 0) {
      if (!hci_acl_.SendSecureSend(0x01, file.view(offset, frag_len))) {
        errorf("Failed sending a command chunk!\n");
        return LoadStatus::kError;
      }
      offset += frag_len;
      frag_len = 0;
    }
  }

  return LoadStatus::kComplete;
}

}  // namespace btintel
