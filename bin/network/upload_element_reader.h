// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_
#define GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_

#include "lib/fxl/macros.h"
#include "zx/socket.h"
#include "zx/vmo.h"

#include <array>
#include <limits>

namespace network {

class UploadElementReader {
 public:
  static constexpr size_t BUFSIZE = 1024;
  static constexpr size_t kUnknownSize = std::numeric_limits<size_t>::max();

  UploadElementReader();
  virtual ~UploadElementReader();

  zx_status_t err() const { return err_; }

  // may produce kUnknownSize
  virtual size_t size() = 0;

  // returns true if and only if content was read or there is potentially more
  // to read
  virtual bool ReadAvailable(std::ostream* os) = 0;

 protected:
  zx_status_t err_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(UploadElementReader);
};

class SocketUploadElementReader : public UploadElementReader {
 public:
  SocketUploadElementReader(zx::socket socket);
  ~SocketUploadElementReader() override;

  // always kUnknownSize
  size_t size() override;
  bool ReadAvailable(std::ostream* os) override;

 private:
  zx::socket socket_;
  std::array<char, BUFSIZE> buf_;
};

class VmoUploadElementReader : public UploadElementReader {
 public:
  VmoUploadElementReader(zx::vmo vmo);
  VmoUploadElementReader(zx::vmo vmo, uint64_t size);
  ~VmoUploadElementReader() override;

  size_t size() override;
  bool ReadAvailable(std::ostream* os) override;

 private:
  zx::vmo vmo_;
  uint64_t size_;
  uint64_t offset_;
  std::array<char, BUFSIZE> buf_;
};

}  // namespace network

#endif  // GARNET_BIN_NETWORK_UPLOAD_ELEMENT_READER_H_
