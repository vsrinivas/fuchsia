// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_

#include <string>

#include "src/lib/fsl/vmo/sized_vmo.h"

namespace forensics {
namespace feedback_data {

// Decodes data previously encoded via an Encoder.
//
// As an Encoder operates on a block, the Decoder needs to be reset when decoding data from another
// block.
class Decoder {
 public:
  virtual ~Decoder(){};

  virtual std::string Decode(const fsl::SizedVmo& vmo) = 0;

  // Resets the state of the decoder. The state can consist of previous data, dictionaries, etc.
  virtual void Reset() = 0;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_
