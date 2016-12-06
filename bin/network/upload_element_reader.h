// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETWORK_UPLOAD_ELEMENT_READER_H_
#define APPS_NETWORK_UPLOAD_ELEMENT_READER_H_

#include "lib/ftl/macros.h"
#include "mx/socket.h"
#include "mx/vmo.h"

#include <array>

namespace network {

class UploadElementReader {
 public:
  static constexpr size_t BUFSIZE = 1024;

  UploadElementReader() {}
  virtual ~UploadElementReader(){};

  virtual mx_status_t ReadAll(std::ostream* os) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(UploadElementReader);
};

class SocketUploadElementReader : public UploadElementReader {
 public:
  SocketUploadElementReader(mx::socket socket);
  ~SocketUploadElementReader() override;

  mx_status_t ReadAll(std::ostream* os) override;

 private:
  mx::socket socket_;
  std::array<char, BUFSIZE> buf_;
};

class VmoUploadElementReader : public UploadElementReader {
 public:
  VmoUploadElementReader(mx::vmo vmo);
  ~VmoUploadElementReader() override;

  mx_status_t ReadAll(std::ostream* os) override;

 private:
  mx::vmo vmo_;
  std::array<char, BUFSIZE> buf_;
};

}  // namespace network

#endif  // APPS_NETWORK_UPLOAD_ELEMENT_READER_H_
