// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/ogg_demux.h"

#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include "src/media/sounds/soundplayer/opus_decoder.h"

namespace soundplayer {
namespace {

static constexpr size_t kReadSize = 4096;

}  // namespace

OggDemux::OggDemux() {}

OggDemux::~OggDemux() { ogg_sync_clear(&sync_state_); }

fit::result<Sound, zx_status_t> OggDemux::Process(int fd) {
  int ogg_result = ogg_sync_init(&sync_state_);
  if (ogg_result != 0) {
    FX_LOGS(WARNING) << "ogg_sync_init failed, result " << ogg_result;
    return fit::error(ZX_ERR_INTERNAL);
  }

  while (ReadPage(fd)) {
    int serial_number = ogg_page_serialno(&page_);
    Stream* stream = GetOrCreateStream(serial_number);
    if (!stream) {
      // We don't want this stream for some reason.
      continue;
    }

    ogg_result = ogg_stream_pagein(&stream->state(), &page_);
    if (ogg_result != 0) {
      FX_LOGS(WARNING) << "ogg_stream_pagein failed, result " << ogg_result;
      return fit::error(ZX_ERR_IO_INVALID);
    }

    while (true) {
      ogg_packet packet;
      ogg_result = ogg_stream_packetout(&stream->state(), &packet);
      if (ogg_result == 0) {
        // Need more data to be able to complete the packet
        break;
      } else if (ogg_result == -1) {
        FX_LOGS(WARNING) << "ogg_stream_packetout failed, result " << ogg_result;
        return fit::error(ZX_ERR_IO_INVALID);
      }

      if (!OnPacket(packet, serial_number)) {
        return fit::error(ZX_ERR_IO_INVALID);
      }
    }
  }

  if (!end_of_file_ || !stream_ || !stream_->decoder()) {
    return fit::error(ZX_ERR_IO_INVALID);
  }

  auto sound = stream_->decoder()->TakeSound();
  stream_ = nullptr;

  return fit::ok(std::move(sound));
}

bool OggDemux::ReadPage(int fd) {
  while (true) {
    int ogg_result = ogg_sync_pageout(&sync_state_, &page_);
    if (ogg_result == 1) {
      return true;
    }

    if (ogg_result != 0) {
      // We fail here if the page doesn't have an ogg file signature. Typically, that will
      // happen on the first call to this method, if the file isn't an ogg file.
      FX_DLOGS(WARNING) << "ogg_sync_pageout failed, result " << ogg_result;
      return false;
    }

    char* buffer = ogg_sync_buffer(&sync_state_, kReadSize);
    if (!buffer) {
      FX_LOGS(WARNING) << "ogg_sync_buffer failed";
      return false;
    }

    ssize_t bytes_read = read(fd, buffer, kReadSize);
    if (bytes_read == -1) {
      return false;
    }

    if (bytes_read == 0) {
      end_of_file_ = true;
      return false;
    }

    ogg_result = ogg_sync_wrote(&sync_state_, bytes_read);
    if (ogg_result != 0) {
      FX_LOGS(WARNING) << "ogg_sync_wrote failed, result " << ogg_result;
      return false;
    }
  }
}

OggDemux::Stream* OggDemux::GetOrCreateStream(int serial_number) {
  if (!stream_) {
    stream_ = Stream::Create(serial_number);
    // If we failed to make a stream here, we'll traverse the entire file without one. When we're
    // done, the logic at the end of |Process| will notice that nothing was decoded, and |Process|
    // will return false.
    return stream_.get();
  }

  if (serial_number == stream_->serial_number()) {
    return stream_.get();
  }

  return nullptr;
}

OggDemux::Stream* OggDemux::GetStream(int serial_number) {
  if (!stream_ || serial_number != stream_->serial_number()) {
    return nullptr;
  }

  return stream_.get();
}

bool OggDemux::RejectStream(Stream* stream) {
  // We only support one stream.
  FX_DCHECK(stream == stream_.get());

  bool had_decoder = stream_->decoder();

  stream_ = nullptr;

  // If we haven't created a decoder, the stream just isn't interesting, and we can safely continue.
  // If it does have a decoder, it was an interesting stream, but the file is apparently corrupt.
  return !had_decoder;
}

bool OggDemux::OnPacket(const ogg_packet& packet, int serial_number) {
  auto stream = GetStream(serial_number);

  if (!stream) {
    FX_LOGS(WARNING) << "ignoring packet for absent stream " << serial_number;
    return true;
  }

  if (packet.b_o_s) {
    if (OpusDecoder::CheckHeaderPacket(packet.packet, packet.bytes)) {
      stream->SetDecoder(std::make_unique<OpusDecoder>());
    } else {
      return RejectStream(stream);
    }
  }

  FX_DCHECK(stream->decoder());
  if (!stream->decoder()->ProcessPacket(packet.packet, packet.bytes, packet.b_o_s, packet.e_o_s)) {
    return RejectStream(stream);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// OggDemux::Stream implementation.

// static
std::unique_ptr<OggDemux::Stream> OggDemux::Stream::Create(int serial_number) {
  auto result = std::make_unique<Stream>(serial_number);

  int ogg_result = ogg_stream_init(&result->state_, serial_number);
  if (ogg_result != 0) {
    FX_LOGS(WARNING) << "ogg_stream_init failed, result " << ogg_result;
    return nullptr;
  }

  return result;
}

OggDemux::Stream::Stream(int serial_number) : serial_number_(serial_number) {}

OggDemux::Stream::~Stream() { ogg_stream_clear(&state_); }

}  // namespace soundplayer
