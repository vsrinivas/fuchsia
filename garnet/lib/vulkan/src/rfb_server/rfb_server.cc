// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rfb_server.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

namespace {
struct pixel_format {
  uint8_t bits_per_pixel;
  uint8_t depth;
  uint8_t big_endian;
  uint8_t true_color;
  uint16_t red_max;
  uint16_t green_max;
  uint16_t blue_max;
  uint8_t red_shift;
  uint8_t green_shift;
  uint8_t blue_shift;
  uint8_t padding[3];
};
}  // namespace

// See https://tools.ietf.org/html/rfc6143 for protocol documentation.
bool RFBServer::Initialize(uint32_t width, uint32_t height, uint32_t port) {
  if (initialization_attempted_) {
    return initialization_succeeded_;
  }
  initialization_attempted_ = true;
  width_ = width;
  height_ = height;

  constexpr uint32_t kHeaderLength = 12;
  const char kHeader[kHeaderLength + 1] = "RFB 003.008\n";
  int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket failed\n");
    return false;
  }

  int enabled = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
                 sizeof(enabled))) {
    perror("so_reuseaddr failed\n");
    return false;
  }
  struct sockaddr_in6 serveraddr;
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin6_family = AF_INET6;
  serveraddr.sin6_port = htons(port);
  serveraddr.sin6_addr = in6addr_any;
  int bind_result =
      ::bind(listen_fd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
  if (bind_result < 0) {
    perror("bind failed\n");
    return false;
  }
  constexpr int kListenBacklog = 10;
  if (listen(listen_fd, kListenBacklog)) {
    perror("listen failed\n");
    return false;
  }

  fprintf(stderr, "RFB layer waiting on port %u\n", port);
  fd_ = fxl::UniqueFD(accept(listen_fd, nullptr, nullptr));
  if (!fd_.is_valid()) {
    perror("accept failed\n");
    return false;
  }

  if (!SendBytes(kHeader, kHeaderLength))
    return false;

  char read_header[kHeaderLength + 1];

  if (ReadEntireMessage(read_header, kHeaderLength) < 0)
    return false;
  if (memcmp(read_header, kHeader, kHeaderLength) != 0) {
    read_header[kHeaderLength] = 0;
    fprintf(stderr, "Received unsupported header value %s\n", read_header);
    return false;
  }

  constexpr uint8_t kSecurityTypeNone = 1;
  uint8_t security_header[] = {1, kSecurityTypeNone};
  if (!SendBytes(security_header, sizeof(security_header)))
    return false;
  uint8_t security_type;
  if (ReadEntireMessage(&security_type, sizeof(security_type)) < 0)
    return false;
  if (security_type != kSecurityTypeNone)
    return false;

  uint32_t security_status = 0;
  if (!SendBytes(&security_status, sizeof(security_status)))
    return false;
  uint8_t shared_flag;  // ignored.
  if (ReadEntireMessage(&shared_flag, sizeof(shared_flag)) < 0)
    return false;
  struct server_init {
    uint16_t width;
    uint16_t height;
    struct pixel_format pixel_format;
    uint32_t name_length;
    uint8_t name_string[5];
  } __attribute__((packed)) server_init;
  server_init.width = htons(width);
  server_init.height = htons(height);
  server_init.pixel_format.bits_per_pixel = 32;
  server_init.pixel_format.depth = 1;
  server_init.pixel_format.big_endian = 0;
  server_init.pixel_format.true_color = 1;
  server_init.pixel_format.red_max = htons(255);
  server_init.pixel_format.green_max = htons(255);
  server_init.pixel_format.blue_max = htons(255);
  server_init.pixel_format.red_shift = 0;
  server_init.pixel_format.green_shift = 8;
  server_init.pixel_format.blue_shift = 16;
  server_init.name_length = htonl(5);
  server_init.name_string[0] = 'm';
  server_init.name_string[1] = 'a';
  server_init.name_string[2] = 'g';
  server_init.name_string[3] = 'm';
  server_init.name_string[4] = 'a';
  if (!SendBytes(&server_init, sizeof(server_init)))
    return false;

  initialization_succeeded_ = true;

  return true;
}

void RFBServer::WaitForFramebufferUpdate() {
  if (!initialization_succeeded_)
    return;
  while (true) {
    uint8_t message_type;
    int return_code = ReadEntireMessage(&message_type, sizeof(message_type));
    if (return_code < 0) {
      perror("ReadEntireMessage failure");
      return;
    }
    switch (message_type) {
      case 0: {
        // In theory the server should transmit messages using this pixel format
        // afterwards, but we just ignore it and use the one that was initially
        // sent to the client.
        struct set_pixel_format {
          uint8_t padding[3];
          pixel_format format;
        } __attribute__((packed)) set_pixel_format;
        if (ReadEntireMessage(&set_pixel_format, sizeof(set_pixel_format)) < 0)
          return;
        break;
      }
      case 2: {
        struct set_encodings {
          uint8_t padding;
          uint16_t encoding_count;
        } __attribute__((packed)) set_encodings;
        if (ReadEntireMessage(&set_encodings, sizeof(set_encodings)) < 0)
          return;
        uint16_t encoding_count = ntohs(set_encodings.encoding_count);
        std::vector<int32_t> encodings(encoding_count);
        if (ReadEntireMessage(encodings.data(), encoding_count * 4) < 0)
          return;
        break;
      }
      case 3: {
        // The server should only send updates for the requested region, but
        // instead we give updates for the entire frame and hope the client can
        // understand.
        struct framebuffer_update_request {
          uint8_t incremental;
          uint16_t x_position;
          uint16_t y_position;
          uint16_t width;
          uint16_t height;
        } __attribute__((packed)) update_request;
        ReadEntireMessage(&update_request, sizeof(update_request));
        return;
      }
      case 4: {
        struct key_event {
          uint8_t down_flag;
          uint8_t padding[2];
          uint32_t key;
        } __attribute__((packed)) key_event;
        if (ReadEntireMessage(&key_event, sizeof(key_event)) < 0)
          return;
        break;
      }
      case 5: {
        struct pointer_event {
          uint8_t button_mask;
          uint16_t x_position;
          uint16_t y_position;
        } __attribute__((packed)) pointer_event;
        if (ReadEntireMessage(&pointer_event, sizeof(pointer_event)) < 0)
          return;
        break;
      }
      case 6: {
        struct cut_text {
          uint8_t padding[3];
          uint32_t length;
        } __attribute__((packed)) cut_text;
        if (ReadEntireMessage(&cut_text, sizeof(cut_text)) < 0)
          return;
        cut_text.length = ntohl(cut_text.length);
        std::vector<uint8_t> text(cut_text.length);
        if (ReadEntireMessage(text.data(), cut_text.length) < 0)
          return;
        break;
      }
    }
  }
}

void RFBServer::StartUpdate() {
  if (!initialization_succeeded_)
    return;
  struct update_header {
    uint8_t type;
    uint8_t padding;
    uint16_t number_of_rectangles;
  } update_header;
  update_header.type = 0;
  update_header.padding = 0;
  update_header.number_of_rectangles = htons(1);
  if (!SendBytes(&update_header, sizeof(update_header)))
    return;
  struct rectangle_header {
    uint16_t x_position;
    uint16_t y_position;
    uint16_t width;
    uint16_t height;
    int32_t encoding_type;
  } rectangle_header;
  rectangle_header.x_position = htons(0);
  rectangle_header.y_position = htons(0);
  rectangle_header.width = htons(width_);
  rectangle_header.height = htons(height_);
  rectangle_header.encoding_type = htonl(0);
  SendBytes(&rectangle_header, sizeof(rectangle_header));
}

bool RFBServer::SendBytes(const void* data, uint32_t length) {
  if (!fd_.is_valid())
    return false;

  const char* current_data = static_cast<const char*>(data);
  uint32_t written = 0;
  while (written < length) {
    int current_write = send(fd_.get(), current_data, length - written, 0);
    if (current_write < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    current_data += current_write;
    written += current_write;
  }
  return true;
}

int RFBServer::ReadEntireMessage(void* data, uint32_t size) {
  uint32_t read = 0;
  char* current_data = static_cast<char*>(data);
  while (read < size) {
    int current_read = recv(fd_.get(), current_data, size - read, 0);
    if (current_read < 0) {
      if (errno == EINTR)
        continue;
      return current_read;
    }
    read += current_read;
    current_data += current_read;
  }
  return read;
}
