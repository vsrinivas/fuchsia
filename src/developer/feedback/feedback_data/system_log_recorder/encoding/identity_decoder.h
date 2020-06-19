// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_DECODER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_DECODER_H_

#include <string>

#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/decoder.h"

namespace feedback {

class IdentityDecoder : public Decoder {
 public:
  IdentityDecoder(){};

  virtual ~IdentityDecoder(){};

  // |Decoder|
  virtual std::string Decode(const fsl::SizedVmo& vmo);
  virtual void Reset(){};
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_DECODER_H_
