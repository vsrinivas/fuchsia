// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <set>
#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {

using AttachmentKey = std::string;
using AttachmentKeys = std::set<AttachmentKey>;

class AttachmentValue {
 public:
  enum class State {
    kComplete,
    kPartial,
    kMissing,
  };

  explicit AttachmentValue(const std::string& value)
      : state_(State::kComplete), value_(value), error_(std::nullopt) {}
  AttachmentValue(const std::string& value, enum Error error)
      : state_(State::kPartial), value_(value), error_(error) {}
  AttachmentValue(enum Error error)
      : state_(State::kMissing), value_(std::nullopt), error_(error) {}

  bool HasValue() const { return value_.has_value(); }

  const std::string& Value() const {
    FX_CHECK(HasValue());
    return value_.value();
  }

  bool HasError() const { return error_.has_value(); }

  enum Error Error() const {
    FX_CHECK(HasError());
    return error_.value();
  }

  enum State State() const { return state_; }

  bool operator==(const AttachmentValue& other) const {
    return (state_ == other.state_) && (value_ == other.value_) && (error_ == other.error_);
  }

 private:
  enum State state_;
  std::optional<std::string> value_;
  std::optional<enum Error> error_;
};

using Attachment = std::pair<AttachmentKey, AttachmentValue>;
using Attachments = std::map<AttachmentKey, AttachmentValue>;

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_
