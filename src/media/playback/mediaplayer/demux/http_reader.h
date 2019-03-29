// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_HTTP_READER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_HTTP_READER_H_

#include <string>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include "lib/component/cpp/startup_context.h"
#include "src/media/playback/mediaplayer/demux/reader.h"
#include "src/media/playback/mediaplayer/util/incident.h"

namespace media_player {

// Reads from a file on behalf of a demux.
class HttpReader : public Reader {
 public:
  static std::shared_ptr<HttpReader> Create(
      component::StartupContext* startup_context, const std::string& url,
      fidl::VectorPtr<fuchsia::net::oldhttp::HttpHeader> headers);

  HttpReader(component::StartupContext* startup_context, const std::string& url,
             fidl::VectorPtr<fuchsia::net::oldhttp::HttpHeader> headers);

  ~HttpReader() override;

  // Reader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(size_t position, uint8_t* buffer, size_t bytes_to_read,
              ReadAtCallback callback) override;

 private:
  // Reads from an open |socket_|.
  void ReadFromSocket();

  // Completes a pending ReadAt.
  void CompleteReadAt(zx_status_t status, size_t bytes_read);

  // Fails the pending ReadAt.
  void FailReadAt(zx_status_t status);

  // Performs an HTTP load and reads from the resulting socket.
  void LoadAndReadFromSocket();

  std::string url_;
  fidl::VectorPtr<fuchsia::net::oldhttp::HttpHeader> headers_;
  ::fuchsia::net::oldhttp::URLLoaderPtr url_loader_;
  zx_status_t status_ = ZX_OK;
  uint64_t size_ = kUnknownSize;
  bool can_seek_ = false;
  zx::socket socket_;
  std::unique_ptr<async::Wait> waiter_;
  size_t socket_position_ = kUnknownSize;
  Incident ready_;

  // Pending ReadAt parameters.
  size_t read_at_position_;
  uint8_t* read_at_buffer_;
  size_t read_at_bytes_to_read_;
  size_t read_at_bytes_remaining_;
  ReadAtCallback read_at_callback_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_HTTP_READER_H_
