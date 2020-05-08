// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_

#include <map>
#include <set>
#include <string>

#include "src/developer/feedback/utils/errors.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using AttachmentKey = std::string;
using AttachmentKeys = std::set<AttachmentKey>;

class AttachmentValue {
 public:
  enum class State {
    kComplete,
    kPartial,
    kMissing,
  };

  AttachmentValue(const std::string& value) : state_(State::kComplete), value_(value) {}
  AttachmentValue(const std::string& value, enum Error error)
      : state_(State::kPartial), value_(value), error_(error) {}
  AttachmentValue(enum Error error) : state_(State::kMissing), error_(error) {}

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

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ATTACHMENTS_TYPES_H_
