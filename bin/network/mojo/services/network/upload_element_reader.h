// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_UPLOAD_ELEMENT_READER_H_
#define MOJO_SERVICES_NETWORK_UPLOAD_ELEMENT_READER_H_

#include "base/macros.h"
#include "mojo/public/cpp/system/data_pipe.h"

#include <array>

namespace mojo {

class UploadElementReader {
 public:
  static constexpr size_t BUFSIZE = 1024;

  UploadElementReader(ScopedDataPipeConsumerHandle pipe);
  ~UploadElementReader();

  MojoResult ReadAll(std::ostream *os);

 private:
  ScopedDataPipeConsumerHandle pipe_;
  std::array<char, BUFSIZE> buf_;

  DISALLOW_COPY_AND_ASSIGN(UploadElementReader);
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_UPLOAD_ELEMENT_READER_H_
