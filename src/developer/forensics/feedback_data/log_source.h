// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_LOG_SOURCE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_LOG_SOURCE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>
#include <vector>

namespace forensics::feedback_data {

// Receives logs emitted by the system.
class LogSink {
 public:
  // A message is a FIDL LogMessage or an error string.
  using MessageOr = ::fpromise::result<fuchsia::logger::LogMessage, std::string>;

  // Adds |message| to the sink.
  //
  // Returns false if the write fails though callers are not expected to take action on failure.
  virtual bool Add(MessageOr message) = 0;
};

// Receives log messages from the system's logging service and dispatches them to a sink.
//
// TODO(fxbug.dev/93059): In the event of an error, i.e. FIDL disconnection, the source _does not_
// attempt to reconnect to the system's logging service. Change this!
class LogSource {
 public:
  LogSource(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            LogSink* sink);

  void Start();
  void Stop();

 private:
  void GetNext();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;

  LogSink* sink_;
  fuchsia::diagnostics::ArchiveAccessorPtr archive_accessor_;
  fuchsia::diagnostics::BatchIteratorPtr batch_iterator_;
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_LOG_SOURCE_H_
