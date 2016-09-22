// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/firebase/event_stream.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/trim.h"

namespace firebase {

EventStream::EventStream() {}

EventStream::~EventStream() {}

void EventStream::Start(
    mojo::ScopedDataPipeConsumerHandle source,
    const std::function<EventCallback>& event_callback,
    const std::function<CompletionCallback>& completion_callback) {
  event_callback_ = event_callback;
  completion_callback_ = completion_callback;
  drainer_.reset(new glue::DataPipeDrainer(this));
  drainer_->Start(std::move(source));
}

void EventStream::OnDataAvailable(const void* data, size_t num_bytes) {
  const char* current = static_cast<const char*>(data);
  const char* const end = current + num_bytes;
  while (current < end) {
    const char* newline = std::find(current, end, '\n');
    pending_line_.append(std::string(current, newline - current));
    current = newline;
    if (newline != end) {
      ProcessLine(pending_line_);
      pending_line_.clear();
      ++current;
    }
  }
}

void EventStream::OnDataComplete() {
  completion_callback_();
}

// See https://www.w3.org/TR/eventsource/#event-stream-interpretation.
void EventStream::ProcessLine(const std::string& line) {
  // If the line is empty, dispatch the event.
  if (line.empty()) {
    // If data is empty, clear event type and abort.
    if (data_.empty()) {
      event_type_.clear();
      return;
    }

    // Remove the trailing line break from data.
    if (*(data_.rbegin()) == '\n') {
      data_.resize(data_.size() - 1);
    }

    event_callback_(Status::OK, event_type_, data_);
    event_type_.clear();
    data_.clear();
    return;
  }

  // If the line starts with a colon, ignore the line.
  if (line[0] == ':') {
    return;
  }

  // If the line contains a colon, process the field.
  size_t colon_pos = line.find(':');
  if (colon_pos != std::string::npos) {
    std::string field(line.substr(0, colon_pos));
    std::string value = line.substr(colon_pos + 1);
    ProcessField(field, ftl::TrimString(value, " ").ToString());
    return;
  }

  // If the line does not contain a colon, process the field using the whole
  // line as the field name and empty string as field value.
  ProcessField(line, "");
}

void EventStream::ProcessField(const std::string& field,
                               const std::string& value) {
  if (field == "event") {
    event_type_ = value;
  } else if (field == "data") {
    data_.append(value);
    data_.append("\n");
  } else if (field == "id" || field == "retry") {
    // Not implemented.
    FTL_LOG(WARNING) << "Event stream - field type not implemented: " << field;
  } else {
    // The spec says to ignore unknown field names.
    FTL_LOG(WARNING) << "Event stream - unknown field name: " << field;
  }
}

}  // namespace firebase
