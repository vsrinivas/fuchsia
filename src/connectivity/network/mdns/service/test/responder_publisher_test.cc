// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include <queue>

#include "src/connectivity/network/mdns/service/services/mdns_deprecated_service_impl.h"

namespace mdns {
namespace test {

class ResponderPublisherTest : public gtest::TestLoopFixture,
                               public fuchsia::net::mdns::PublicationResponder {
 public:
  struct OnPublicationCall {
    OnPublicationCall(fuchsia::net::mdns::PublicationCause publication_cause,
                      fidl::StringPtr subtype,
                      std::vector<fuchsia::net::IpAddress> source_addresses,
                      OnPublicationCallback callback)
        : publication_cause_(publication_cause),
          subtype_(subtype),
          source_addresses_(std::move(source_addresses)),
          callback_(std::move(callback)) {}

    fuchsia::net::mdns::PublicationCause publication_cause_;
    fidl::StringPtr subtype_;
    std::vector<fuchsia::net::IpAddress> source_addresses_;
    OnPublicationCallback callback_;
  };

  ResponderPublisherTest() : binding_(this) {}

  ~ResponderPublisherTest() override = default;

  void Bind(fidl::InterfaceRequest<fuchsia::net::mdns::PublicationResponder> request) {
    binding_.Bind(std::move(request));
  }

  std::queue<OnPublicationCall>& on_publication_calls() { return on_publication_calls_; }

  // fuchsia::net::mdns::PublicationResponder implementation.
  void OnPublication(fuchsia::net::mdns::PublicationCause publication_cause,
                     fidl::StringPtr subtype, std::vector<fuchsia::net::IpAddress> source_addresses,
                     OnPublicationCallback callback) override {
    on_publication_calls_.emplace(publication_cause, subtype, std::move(source_addresses),
                                  std::move(callback));
  }

 private:
  fidl::Binding<fuchsia::net::mdns::PublicationResponder> binding_;
  std::queue<OnPublicationCall> on_publication_calls_;
};

// Tests that flow control of |OnPublication| calls works properly.
TEST_F(ResponderPublisherTest, FlowControl) {
  fuchsia::net::mdns::PublicationResponderPtr responder;
  Bind(responder.NewRequest());

  bool deleter_called = false;
  MdnsDeprecatedServiceImpl::ResponderPublisher under_test(
      std::move(responder),
      [](fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result) {},
      [&deleter_called]() { deleter_called = true; });

  // Ask the publisher for a publication and expect that the request is forwarded over FIDL.
  under_test.GetPublication(Mdns::PublicationCause::kAnnouncement, "1", {},
                            [](std::unique_ptr<Mdns::Publication> publication) {});
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_publication_calls().size());

  // Ask the publisher for a second publication and expect that this one is also forwarded.
  under_test.GetPublication(Mdns::PublicationCause::kAnnouncement, "2", {},
                            [](std::unique_ptr<Mdns::Publication> publication) {});
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_publication_calls().size());

  // Ask the publisher for a third publication. Expect that it's not forwarded yet, because we
  // haven't responded to either of the first two.
  under_test.GetPublication(Mdns::PublicationCause::kAnnouncement, "3", {},
                            [](std::unique_ptr<Mdns::Publication> publication) {});
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_publication_calls().size());

  // Respond to the first request and expect the third request to be forwarded.
  EXPECT_EQ("1", on_publication_calls().front().subtype_);
  on_publication_calls().front().callback_(nullptr);
  on_publication_calls().pop();
  EXPECT_EQ(1u, on_publication_calls().size());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_publication_calls().size());

  // Ask the publisher for a fourth publication. Expect that it's not forwarded yet, because we
  // haven't responded to either of the second and third requests.
  under_test.GetPublication(Mdns::PublicationCause::kAnnouncement, "4", {},
                            [](std::unique_ptr<Mdns::Publication> publication) {});
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_publication_calls().size());

  // Respond to the second request and expect the fourth request to be forwarded.
  EXPECT_EQ("2", on_publication_calls().front().subtype_);
  on_publication_calls().front().callback_(nullptr);
  on_publication_calls().pop();
  EXPECT_EQ(1u, on_publication_calls().size());
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_publication_calls().size());

  // Respond to the third and fourth requests.
  EXPECT_EQ("3", on_publication_calls().front().subtype_);
  on_publication_calls().front().callback_(nullptr);
  on_publication_calls().pop();
  EXPECT_EQ("4", on_publication_calls().front().subtype_);
  on_publication_calls().front().callback_(nullptr);
  on_publication_calls().pop();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, on_publication_calls().size());

  EXPECT_FALSE(deleter_called);
}

}  // namespace test
}  // namespace mdns
