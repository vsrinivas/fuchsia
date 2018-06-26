// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/event_stream.h"

#include <memory>
#include <string>
#include <utility>

#include <lib/async/cpp/task.h>

#include "lib/fsl/socket/strings.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/lib/socket/socket_pair.h"

namespace firebase {

class EventStreamTest : public ::gtest::TestLoopFixture {
 public:
  EventStreamTest() {}
  ~EventStreamTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    socket::SocketPair socket;
    producer_socket_ = std::move(socket.socket1);
    event_stream_ = std::make_unique<EventStream>();
    event_stream_->Start(std::move(socket.socket2),
                         [this](Status status, const std::string& event,
                                const std::string& data) {
                           status_.push_back(status);
                           events_.push_back(event);
                           data_.push_back(data);
                           if (delete_on_event_) {
                             event_stream_.reset();
                           }
                         },
                         []() {});
  }

  void TearDown() override {
    producer_socket_.reset();
    ::testing::Test::TearDown();
  }

  void Feed(const std::string& data) {
    event_stream_->OnDataAvailable(data.data(), data.size());
  }

  void Done() { event_stream_->OnDataComplete(); }

  zx::socket producer_socket_;
  std::unique_ptr<EventStream> event_stream_;
  std::vector<Status> status_;
  std::vector<std::string> events_;
  std::vector<std::string> data_;
  bool delete_on_event_ = false;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EventStreamTest);
};

namespace {

TEST_F(EventStreamTest, OneEvent) {
  Feed("event: abc\ndata: bazinga\n\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

TEST_F(EventStreamTest, CutAtLineBreaks) {
  Feed("event: abc\n");
  Feed("data: bazinga\n");
  Feed("\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

TEST_F(EventStreamTest, CutInsideTag) {
  Feed("eve");
  Feed("nt: abc\n");
  Feed("da");
  Feed("ta: bazinga\n\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

TEST_F(EventStreamTest, CutInsideData) {
  Feed("event: abc\n");
  Feed("data: baz");
  Feed("inga\n\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

TEST_F(EventStreamTest, DataInMultipleLines) {
  Feed("event: abc\n");
  Feed("data: baz\n");
  Feed("data: in\ndata: ga\n");
  Feed("\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("baz\nin\nga", data_[0]);
}

TEST_F(EventStreamTest, IgnoreUnknownFieldNames) {
  Feed("event: abc\n");
  Feed("data: baz\n");
  Feed("trololo: trololo\n");  // Unknown field name.
  Feed("data: in\ndata: ga\n");
  Feed("\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("baz\nin\nga", data_[0]);
}

TEST_F(EventStreamTest, IgnoreLinesStartingWithColon) {
  Feed("event: abc\n");
  Feed(":ignore me, trololo\n");
  Feed("data: bazinga\n\n");
  Done();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

TEST_F(EventStreamTest, MultipleEvents) {
  Feed("event: abc\n");
  Feed("data: bazinga\n\n");
  Feed("event: cde\n");
  Feed("data: 42\n\n");
  Feed("event: fgh\n");
  Feed("data: 50\n\n");
  Done();

  EXPECT_EQ(3u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);

  EXPECT_EQ(Status::OK, status_[1]);
  EXPECT_EQ("cde", events_[1]);
  EXPECT_EQ("42", data_[1]);

  EXPECT_EQ(Status::OK, status_[2]);
  EXPECT_EQ("fgh", events_[2]);
  EXPECT_EQ("50", data_[2]);
}

TEST_F(EventStreamTest, DeleteOnEvent) {
  delete_on_event_ = true;
  fsl::BlockingCopyFromString("event: abc\ndata: bazinga\n\n",
                              producer_socket_);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, status_.size());
  EXPECT_EQ(Status::OK, status_[0]);
  EXPECT_EQ("abc", events_[0]);
  EXPECT_EQ("bazinga", data_[0]);
}

}  // namespace
}  // namespace firebase
