// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/event_stream.h"

#include <utility>

#include <lib/fit/function.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/trim.h"

namespace firebase {

EventStream::EventStream() {}

EventStream::~EventStream() {}

void EventStream::Start(zx::socket source,
                        fit::function<EventCallback> event_callback,
                        fit::function<CompletionCallback> completion_callback) {
  event_callback_ = std::move(event_callback);
  completion_callback_ = std::move(completion_callback);
  drainer_ = std::make_unique<fsl::SocketDrainer>(this);
  drainer_->Start(std::move(source));
}

void EventStream::OnDataAvailable(const void* data, size_t num_bytes) {
  const char* current = static_cast<const char*>(data);
  const char* const end = current + num_bytes;
  while (current < end) {
    const char* newline = std::find(current, end, '\n');
    pending_line_.append(current, newline - current);
    current = newline;
    if (newline != end) {
      if (!ProcessLine(pending_line_))
        return;
      pending_line_.clear();
      ++current;
    }
  }
}

void EventStream::OnDataComplete() { completion_callback_(); }

// See https://www.w3.org/TR/eventsource/#event-stream-interpretation.
bool EventStream::ProcessLine(fxl::StringView line) {
  // If the line is empty, dispatch the event.
  if (line.empty()) {
    // If data is empty, clear event type and abort.
    if (data_.empty()) {
      event_type_.clear();
      return true;
    }

    // Remove the trailing line break from data.
    if (*(data_.rbegin()) == '\n') {
      data_.resize(data_.size() - 1);
    }

    if (destruction_sentinel_.DestructedWhile(
            [this] { event_callback_(Status::OK, event_type_, data_); })) {
      return false;
    }
    event_type_.clear();
    data_.clear();
    return true;
  }

  // If the line starts with a colon, ignore the line.
  if (line[0] == ':') {
    return true;
  }

  // If the line contains a colon, process the field.
  size_t colon_pos = line.find(':');
  if (colon_pos != std::string::npos) {
    fxl::StringView field(line.substr(0, colon_pos));
    fxl::StringView value = line.substr(colon_pos + 1);
    ProcessField(field, fxl::TrimString(value, " "));
    return true;
  }

  // If the line does not contain a colon, process the field using the whole
  // line as the field name and empty string as field value.
  ProcessField(line, "");
  return true;
}

void EventStream::ProcessField(fxl::StringView field, fxl::StringView value) {
  if (field == "event") {
    event_type_ = value.ToString();
  } else if (field == "data") {
    data_.append(value.data(), value.size());
    data_.append("\n");
  } else if (field == "id" || field == "retry") {
    // Not implemented.
    FXL_LOG(WARNING) << "Event stream - field type not implemented: " << field;
  } else {
    // The spec says to ignore unknown field names.
    FXL_LOG(WARNING) << "Event stream - unknown field name: " << field;
  }
}

}  // namespace firebase
