// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/log_connector_impl.h"

#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace component {
namespace {

class LogConnectorImplTest : public gtest::RealLoopFixture {
 protected:
  LogConnectorImplTest() {}
};

// A LogConnectionListener utility used for testing. Upon construction, takes the consumer using
// thet log connector and forwards any new connections.
class FakeLogConnectionListener : fuchsia::sys::internal::LogConnectionListener {
 public:
  FakeLogConnectionListener(
      LogConnectorImpl* connector_impl,
      fit::function<void(fuchsia::sys::internal::LogConnection)> on_new_connection)
      : binding_(this), on_new_connection_(std::move(on_new_connection)) {
    connector_impl->AddConnectorClient(connector_.NewRequest());
    connector_->TakeLogConnectionListener(
        [&](fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> req) {
          binding_.Bind(std::move(req));
        });
  }

 private:
  // |fuchsia::sys::internal::LogConnectionListener|
  void OnNewConnection(fuchsia::sys::internal::LogConnection connection) override {
    on_new_connection_(std::move(connection));
  }

  fidl::Binding<fuchsia::sys::internal::LogConnectionListener> binding_;
  fit::function<void(fuchsia::sys::internal::LogConnection)> on_new_connection_;
  fuchsia::sys::internal::LogConnectorPtr connector_;
};

// Test that there can only be one LogConnectionListener connection for a LogConnectorImpl
// per-realm.
TEST_F(LogConnectorImplTest, OneConsumerPerRealm) {
  LogConnectorImpl log_conn_impl("realm1");
  fuchsia::sys::internal::LogConnectorPtr log_conn;

  log_conn_impl.AddConnectorClient(log_conn.NewRequest());

  fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> consumer_req;
  log_conn->TakeLogConnectionListener(
      [&](fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> req) {
        consumer_req = std::move(req);
      });
  // Wait until the above TakeLogConnectionListener call responds.
  RunLoopUntil([&] { return consumer_req.is_valid(); });

  // Calling TakeLogConnectionListener() again should return a null request<LogConnectionListener>
  // since we already received a valid one above.
  bool response_received = false;
  fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> consumer_req2;
  log_conn->TakeLogConnectionListener(
      [&](fidl::InterfaceRequest<fuchsia::sys::internal::LogConnectionListener> req) {
        response_received = true;
        consumer_req2 = std::move(req);
      });
  RunLoopUntil([&] { return response_received; });
  EXPECT_FALSE(consumer_req2.is_valid());
}

// Test that log sinks are attributed per connection, and attributed to a component's identity
// (realm path and component URL).
TEST_F(LogConnectorImplTest, AttributedSourceIdentity) {
  const char kRootRealm[] = "root_realm";
  LogConnectorImpl root(kRootRealm);
  std::vector<fuchsia::sys::internal::LogConnection> connections;
  FakeLogConnectionListener root_log_consumer(
      &root, /* on_new_connection */ [&](fuchsia::sys::internal::LogConnection conn) {
        connections.push_back(std::move(conn));
      });

  const char kChildRealm[] = "child_realm";
  const char kGrandChildRealm[] = "grandchild_realm";
  auto child = root.NewChild(kChildRealm);
  auto grandchild = child->NewChild(kGrandChildRealm);

  fuchsia::logger::LogSinkPtr child_log_sink;
  const char kFakeComponentUrl[] = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";
  // Add connection and wait until we intercept it
  grandchild->AddLogConnection(kFakeComponentUrl, "-1", child_log_sink.NewRequest());
  RunLoopUntil([&] { return connections.size() == 1; });

  EXPECT_EQ(kFakeComponentUrl, connections[0].source_identity.component_url());
  EXPECT_EQ("test.cmx", connections[0].source_identity.component_name());
  EXPECT_EQ(std::vector<std::string>({kChildRealm, kGrandChildRealm}),
            connections[0].source_identity.realm_path());
}

// Test that log sinks at the root are attributed without the "sys" prefix so
// that they are aligned with how lifecycle events are attributed.
TEST_F(LogConnectorImplTest, AttributedSysSourceIdentity) {
  const char kRootRealm[] = "app";
  LogConnectorImpl root(kRootRealm);
  std::vector<fuchsia::sys::internal::LogConnection> connections;
  FakeLogConnectionListener root_log_consumer(
      &root, /* on_new_connection */ [&](fuchsia::sys::internal::LogConnection conn) {
        connections.push_back(std::move(conn));
      });

  const char kSysRealm[] = "sys";
  const char kRealm[] = "foo";
  auto child = root.NewChild(kSysRealm);
  auto grandchild = child->NewChild(kRealm);

  fuchsia::logger::LogSinkPtr child_log_sink;
  const char kFakeComponentUrl[] = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";
  // Add connection and wait until we intercept it
  grandchild->AddLogConnection(kFakeComponentUrl, "-1", child_log_sink.NewRequest());
  RunLoopUntil([&] { return connections.size() == 1; });

  EXPECT_EQ(kFakeComponentUrl, connections[0].source_identity.component_url());
  EXPECT_EQ("test.cmx", connections[0].source_identity.component_name());
  EXPECT_EQ(std::vector<std::string>({kRealm}), connections[0].source_identity.realm_path());
}

}  // namespace
}  // namespace component
