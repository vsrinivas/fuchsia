// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "mojo/public/cpp/system/wait.h"
#include "mojo/services/network/upload_element_reader.h"

namespace mojo {

UploadElementReader::UploadElementReader(ScopedDataPipeConsumerHandle pipe)
  : pipe_(pipe.Pass()) {
}

UploadElementReader::~UploadElementReader() {}

MojoResult UploadElementReader::ReadAll(std::ostream *os) {
  MojoResult result = MOJO_RESULT_OK;

  while (true) {
    uint32_t num_bytes = buf_.size();
    result = ReadDataRaw(pipe_.get(), (void*)buf_.data(),
                         &num_bytes, MOJO_READ_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_SHOULD_WAIT) {
      result = Wait(pipe_.get(),
                    MOJO_HANDLE_SIGNAL_READABLE,
                    MOJO_DEADLINE_INDEFINITE,
                    nullptr);
      if (result == MOJO_RESULT_OK)
        continue; // retry now that the data pipe is ready
    }

    if (result != MOJO_RESULT_OK) {
      // If the other end closes the data pipe,
      // we get MOJO_RESULT_FAILED_PRECONDITION.
      if (result == MOJO_RESULT_FAILED_PRECONDITION) {
        result = MOJO_RESULT_OK;
        break;
      }
      LOG(ERROR) << "UploadELementReader: result=" << result;
      break;
    }

    os->write(buf_.data(), num_bytes);
    if (!*os) {
      // TODO(toshik): better result code?
      result = MOJO_RESULT_RESOURCE_EXHAUSTED;
      LOG(ERROR) << "UploadElementReader: result=" << result;
      break;
    }
  }

  return result;
}

}  // namespace mojo
