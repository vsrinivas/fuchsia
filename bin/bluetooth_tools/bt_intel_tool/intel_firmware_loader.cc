// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel_firmware_loader.h"

#include <endian.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <limits>

#include <async/default.h>
#include <async/loop.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <zircon/status.h>
#include <zx/event.h>
#include <zx/time.h>
#include <zx/timer.h>

#include "bt_intel.h"

namespace bt_intel {

namespace {

// A file mapped into memory that we can grab chunks from.
class MemoryFile {
 public:
  MemoryFile(const std::string& filename)
      : fd_(open(filename.c_str(), O_RDONLY)) {
    if (!bool(fd_)) {
      std::cerr << "Failed to open file " << filename << " : "
                << strerror(errno) << std::endl;
      return;
    }

    struct stat file_stats;
    fstat(fd_.get(), &file_stats);
    size_t size = file_stats.st_size;
    std::cerr << "Mapping " << size << " bytes of " << filename << std::endl;

    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd_.get(), 0);
    if (mapped == MAP_FAILED) {
      std::cerr << "Failed to map file to memory: " << strerror(errno)
                << std::endl;
      mapped = nullptr;
    }

    view_ = ::btlib::common::BufferView(mapped, size);
  };

  ~MemoryFile() {
    if (is_valid()) {
      munmap((void*)view_.data(), view_.size());
    }
  }

  size_t size() const { return view_.size(); }

  bool is_valid() const { return view_.data() != nullptr; }

  const uint8_t* at(size_t offset) const { return view_.data() + offset; }

  ::btlib::common::BufferView view(
      size_t offset,
      size_t length = std::numeric_limits<size_t>::max()) const {
    if (!is_valid()) {
      return ::btlib::common::BufferView();
    }
    return view_.view(offset, length);
  }

 private:
  // The actual file descriptor.
  fbl::unique_fd fd_;

  // The view of the whole memory mapped file.
  ::btlib::common::BufferView view_;
};

constexpr size_t kMaxSecureSendArgLen = 252;

bool SecureSend(CommandChannel* channel,
                uint8_t type,
                const ::btlib::common::BufferView& bytes) {
  size_t left = bytes.size();
  bool abort = false;
  while (left > 0 && !abort) {
    size_t frag_len = fbl::min(left, kMaxSecureSendArgLen);
    auto cmd = ::btlib::hci::CommandPacket::New(kSecureSend, frag_len + 1);
    auto data = cmd->mutable_view()->mutable_payload_data();
    data[0] = type;
    data.Write(bytes.view(bytes.size() - left, frag_len), 1);

    channel->SendCommandSync(cmd->view(), [&abort](const auto& event) {
      if (event.event_code() == ::btlib::hci::kCommandCompleteEventCode) {
        const auto& event_params =
            event.view()
                .template payload<::btlib::hci::CommandCompleteEventParams>();
        if (le16toh(event_params.command_opcode) != kSecureSend) {
          std::cerr << "\nIntelFirmwareLoader: Received command complete for "
                       "something else!"
                    << std::endl;
        } else if (event_params.return_parameters[0] != 0x00) {
          printf(
              "\nIntelFirmwareLoader: Received %x for Command Complete, "
              "aborting!\n",
              event_params.return_parameters[0]);
          abort = true;
        }
        return;
      }
      if (event.event_code() ==
          ::btlib::hci::kVendorDebugEventCode) {  // Vendor Event Code
        const auto& params =
            event.view().template payload<IntelSecureSendEventParams>();
        printf(
            "\nIntelFirmwareLoader: SecureSend result %x, opcode: %x, status: "
            "%x",
            params.result, params.opcode, params.status);
        if (params.result) {
          printf(
              "\nIntelFirmwareLoader: Result of %d indicates some error, "
              "aborting!\n",
              params.result);
          abort = true;
          return;
        }
      }
    });
    left -= frag_len;
  }
  if (abort) {
    printf("IntelFirmwareLoader: SecureSend failed at %lu / %zu\n",
           bytes.size() - left, bytes.size());
  }
  return !abort;
}

}  // namespace

IntelFirmwareLoader::LoadStatus IntelFirmwareLoader::LoadBseq(
    const std::string& filename) {
  MemoryFile file(filename);

  if (!file.is_valid()) {
    std::cerr << "Failed to open firmware file." << std::endl;
    return LoadStatus::kError;
  }

  size_t ptr = 0;

  // A bseq file consists of a sequence of:
  // - [0x01] [command w/params]
  // - [0x02] [expected event w/params]
  while (file.size() - ptr > sizeof(::btlib::hci::CommandHeader)) {
    // Parse the next items
    if (*file.at(ptr) != 0x01) {
      std::cerr << "IntelFirmwareLoader: Error: malformed file, expected "
                   "Command Packet marker"
                << std::endl;
      return LoadStatus::kError;
    }
    ptr++;
    ::btlib::common::BufferView command_view = file.view(ptr);
    ::btlib::common::PacketView<::btlib::hci::CommandHeader> command(
        &command_view);
    command = ::btlib::common::PacketView<::btlib::hci::CommandHeader>(
        &command_view, command.header().parameter_total_size);
    ptr += command.size();
    if ((file.size() <= ptr) || (*file.at(ptr) != 0x02)) {
      std::cerr << "IntelFirmwareLoader: Error: malformed file, expected Event "
                   "Packet marker"
                << std::endl;
      return LoadStatus::kError;
    }
    std::deque<::btlib::common::BufferView> events;
    while ((file.size() <= ptr) || (*file.at(ptr) == 0x02)) {
      ptr++;
      auto event = ::btlib::hci::EventPacket::New(0u);
      memcpy(event->mutable_view()->mutable_header(), file.at(ptr),
             sizeof(::btlib::hci::EventHeader));
      event->InitializeFromBuffer();
      size_t event_size = event->view().size();
      events.emplace_back(file.view(ptr, event_size));
      ptr += event_size;
    }

    if (!RunCommandAndExpect(command, events)) {
      return LoadStatus::kError;
    }
  }

  return LoadStatus::kComplete;
}

bool IntelFirmwareLoader::LoadSfi(const std::string& filename) {
  MemoryFile file(filename);

  if (file.size() < 644) {
    std::cerr << "IntelFirmwareLoader: SFI file is too small: " << file.size()
              << " < 644" << std::endl;
    return false;
  }

  size_t ptr = 0;
  // SFI File format:
  // [128 bytes CSS Header]
  if (!SecureSend(channel_, 0x00, file.view(ptr, 128))) {
    std::cerr << "IntelFirmwareLoader: Failed sending CSS Header!" << std::endl;
    return false;
  }
  ptr += 128;
  // [256 bytes PKI]
  if (!SecureSend(channel_, 0x03, file.view(ptr, 256))) {
    std::cerr << "IntelFirmwareLoader: Failed sending PKI Header!" << std::endl;
    return false;
  }
  ptr += 256;
  // There are 4 bytes of unknown data here, that need to be skipped
  // for the file format to be correct later (command sequences)
  ptr += 4;
  // [256 bytes signature info]
  if (!SecureSend(channel_, 0x02, file.view(ptr, 256))) {
    std::cerr << "IntelFirmwareLoader: Failed sending signature Header!"
              << std::endl;
    return false;
  }
  ptr += 256;

  size_t frag_len = 0;
  // [N bytes of command packets, arranged so that the "Secure send" command
  // param size can be a multiple of 4 bytes]
  while (ptr < file.size()) {
    auto next_cmd = file.view(ptr + frag_len);
    ::btlib::common::PacketView<::btlib::hci::CommandHeader> header(&next_cmd);
    size_t cmd_size = sizeof(::btlib::hci::CommandHeader) +
                      header.header().parameter_total_size;
    frag_len += cmd_size;
    if ((frag_len % 4) == 0) {
      if (!SecureSend(channel_, 0x01, file.view(ptr, frag_len))) {
        std::cerr << "Failed sending a command chunk!" << std::endl;
        return false;
      }
      ptr += frag_len;
      frag_len = 0;
    }
  }

  return false;
}

bool IntelFirmwareLoader::RunCommandAndExpect(
    const ::btlib::common::PacketView<::btlib::hci::CommandHeader>& command,
    std::deque<::btlib::common::BufferView>& events) {
  bool failed = false;
  auto event_cb = [&events,
                   &failed](const ::btlib::hci::EventPacket& evt_packet) {
    auto expected = events.front();
    if (evt_packet.view().size() != expected.size()) {
      failed = true;
      return;
    }
    if (memcmp(evt_packet.view().data().data(), expected.data(),
               expected.size()) != 0) {
      failed = true;
      return;
    }
    events.pop_front();
  };

  channel_->SetEventCallback(event_cb);

  channel_->SendCommand(command);

  zx::timer timeout;
  zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timeout);
  zx_status_t status = ZX_OK;
  while (!failed && events.size() > 0) {
    async_loop_run(async_get_default(), zx::deadline_after(ZX_MSEC(10)), true);
    status = timeout.wait_one(ZX_TIMER_SIGNALED, 0u, nullptr);
    if (status != ZX_ERR_TIMED_OUT) {
      if (status == ZX_OK)
        status = ZX_ERR_TIMED_OUT;
      break;
    }
  }

  channel_->SetEventCallback(nullptr);

  if (failed) {
    std::cerr << "IntelFirmwareLoader: unexpected event received"
              << zx_status_get_string(status) << std::endl;
    return false;
  }

  if (status != ZX_OK) {
    std::cerr << "IntelFirmwareLoader: error waiting for events"
              << zx_status_get_string(status) << std::endl;
    return false;
  }

  return true;
}

}  // namespace bt_intel
