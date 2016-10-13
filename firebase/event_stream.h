// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_FIREBASE_EVENT_STREAM_H_
#define APPS_LEDGER_FIREBASE_EVENT_STREAM_H_

#include <functional>
#include <memory>
#include <string>

#include "apps/ledger/firebase/status.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace firebase {

// TODO(ppi): Use a client interface instead.
using EventCallback = void(Status status,
                           const std::string& event,
                           const std::string& data);
using CompletionCallback = void();

// Data pipe drainer that parses a stream of Server-Sent Events.
// Data format of the stream is specified in http://www.w3.org/TR/eventsource/.
class EventStream : public mtl::DataPipeDrainer::Client {
 public:
  EventStream();
  ~EventStream() override;

  void Start(mojo::ScopedDataPipeConsumerHandle source,
             const std::function<EventCallback>& event_callback,
             const std::function<CompletionCallback>& completion_callback);

 private:
  friend class EventStreamTest;

  // mojo::common::DataPipeDrainer::Client:
  void OnDataAvailable(const void* data, size_t num_bytes) override;
  void OnDataComplete() override;

  void ProcessLine(ftl::StringView line);

  void ProcessField(ftl::StringView field, ftl::StringView value);

  std::function<EventCallback> event_callback_;
  std::function<CompletionCallback> completion_callback_;

  // Unprocessed part of the current line.
  std::string pending_line_;
  std::string data_;
  std::string event_type_;

  std::unique_ptr<mtl::DataPipeDrainer> drainer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EventStream);
};

}  // namespace firebase

#endif  // APPS_LEDGER_FIREBASE_EVENT_STREAM_H_
