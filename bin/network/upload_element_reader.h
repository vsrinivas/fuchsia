// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_
#define GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_

#include "lib/fxl/macros.h"
#include "zx/socket.h"
#include "zx/vmo.h"

#include <array>

namespace network {

class UploadElementReader {
 public:
  static constexpr size_t BUFSIZE = 1024;

  UploadElementReader() {}
  virtual ~UploadElementReader(){};

  virtual zx_status_t ReadAll(std::ostream* os) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(UploadElementReader);
};

class SocketUploadElementReader : public UploadElementReader {
 public:
  SocketUploadElementReader(zx::socket socket);
  ~SocketUploadElementReader() override;

  zx_status_t ReadAll(std::ostream* os) override;

 private:
  zx::socket socket_;
  std::array<char, BUFSIZE> buf_;
};

class VmoUploadElementReader : public UploadElementReader {
 public:
  VmoUploadElementReader(zx::vmo vmo);
  ~VmoUploadElementReader() override;

  zx_status_t ReadAll(std::ostream* os) override;

 private:
  zx::vmo vmo_;
  std::array<char, BUFSIZE> buf_;
};

}  // namespace network

#endif  // GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_
