// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TEST_LOCAL_ROUGHTIME_SERVER_H_
#define GARNET_BIN_NETWORK_TIME_TEST_LOCAL_ROUGHTIME_SERVER_H_

#include <thread>
#include "settable_time_source.h"
#include "third_party/roughtime/server.h"
#include "third_party/roughtime/simple_server.h"

namespace time_server {

// A wrapper around Roughtime's simple server for hermetic tests. Returns a
// static time value set at creation time or updated using |SetTime|. Does not
// automatically increment the time.
//
// Construct using |LocalRoughtimeServer::MakeInstance|.
class LocalRoughtimeServer {
 public:
  // Factory method.
  static std::unique_ptr<LocalRoughtimeServer> MakeInstance(
      const uint8_t private_key[], uint16_t preferred_port_number,
      roughtime::rough_time_t initial_time_micros);

  // Starts the server. It will run in a loop until |Stop| is called, so it must
  // be started in a separate thread.
  void Start();

  // Stops the server.
  void Stop();

  // Returns the true if the server is running.
  bool IsRunning();

  // Sets the constant time that is returned by the server.
  void SetTime(roughtime::rough_time_t server_time_micros);

  // Sets the constant time that is returned by the server.
  //
  // Params:
  //   year: four-digit year (e.g. 2019)
  //   month: 1-12
  //   day: 1-31
  //   hour: 0-23
  //   min: 0-59
  //   sec: 0-59
  void SetTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);

  // Gets the server port number, which can differ from the port requested in
  // |MakeInstance| if that port was already taken.
  uint16_t GetPortNumber() const;

 private:
  // Private because it should only be accessed through a factory method.
  explicit LocalRoughtimeServer(SettableTimeSource* time_source,
                                std::unique_ptr<roughtime::SimpleServer> simple_server,
                                uint16_t port_number);

  // Not owned.
  SettableTimeSource* const time_source_;

  std::unique_ptr<roughtime::SimpleServer> simple_server_;

  uint16_t port_number_ = 0;
  std::atomic<bool> is_running_;
};

}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TEST_LOCAL_ROUGHTIME_SERVER_H_
