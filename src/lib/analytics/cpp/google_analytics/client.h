// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_CLIENT_H_
#define SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_CLIENT_H_

#include <lib/fit/promise.h>

#include <string>

#include "src/lib/analytics/cpp/google_analytics/event.h"

namespace analytics::google_analytics {

enum class NetErrorType { kConnectionError, kUnexpectedResponseCode, kAbandoned };

class NetError {
 public:
  explicit NetError(NetErrorType type, std::string_view details = std::string_view())
      : type_(type), details_(details) {}

  NetErrorType type() const { return type_; }
  const std::string& details() const { return details_; }

 private:
  NetErrorType type_;
  std::string details_;
};

// This is an abstract class for Google Analytics client, where the actual HTTP communications are
// left unimplemented. This is because to provide non-blocking HTTP communications, we have to rely
// on certain async mechanism (such as message loop), which is usually chosen by the embedding app.
// To use this class, the embedding app only needs to implement the SendData() method.
//
// Example usage:
//
//     auto ga_client = SomeClientImplementation();
//     ga_client.set_tracking_id("UA-123456-1");
//     ga_client.set_client_id("5555");
//     ga_client.set_user_agent("Example Agent")
//     int64_t value = 12345;
//     auto event = Event("category", "action", "label", value);
//     fit::promise<void, NetError> p = ga_client.AddEvent(event)
//     // Run p in the executor and process the result
//
// For an example implementation, please see
// //src/developer/debug/zxdb/console/google_analytics_client.[cc,h]
// For a full usage example, please see
// //src/developer/debug/zxdb/console/google_analytics_client_manualtest.[cc,h]
class Client {
 public:
  static constexpr char kEndpoint[] = "https://www.google-analytics.com/collect";

  Client() = default;
  Client(const Client&) = delete;

  virtual ~Client() = default;

  void set_user_agent(std::string_view user_agent) { user_agent_ = user_agent; }
  void set_tracking_id(std::string_view tracking_id) { tracking_id_ = tracking_id; }
  void set_client_id(std::string_view client_id) { client_id_ = client_id; }

  fit::promise<void, NetError> AddEvent(const Event& event) const;

 private:
  bool IsReady() const;
  std::map<std::string, std::string> PrepareParameters() const;
  virtual fit::promise<void, NetError> SendData(
      std::string_view user_agent, const std::map<std::string, std::string>& parameters) const = 0;

  std::string user_agent_;
  std::string tracking_id_;
  std::string client_id_;
};

}  // namespace analytics::google_analytics

#endif  // SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_CLIENT_H_
