// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <lib/fit/defer.h>
#include <lib/fit/result.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <zircon/lookup.h>

#include <gtest/gtest.h>

struct GetAddrInfoRequest {
  const char* node;
  const char* service;
  const addrinfo hints;
};

struct GetAddrInfoResponse {
  const in_port_t port;
  const char* address;
  const size_t count;
};

class GetAddrInfoTest : public testing::TestWithParam<
                            std::tuple<GetAddrInfoRequest, fit::result<int, GetAddrInfoResponse>>> {
};

std::string to_string(const GetAddrInfoTest::ParamType& param) {
  const auto& [request, expectation] = param;

  std::ostringstream ss;
  ss << request.service;
  switch (request.hints.ai_socktype) {
    case SOCK_STREAM: {
      ss << "Stream";
    } break;
    case SOCK_DGRAM: {
      ss << "Datagram";
    } break;
    default:
      ss << "Socktype" << request.hints.ai_socktype;
  }
  if (expectation.is_error()) {
    ss << "Fails";  // Can't use gai_strerror here because the returned string contains spaces.
  } else {
    const GetAddrInfoResponse& response = expectation.value();
    ss << "Succeeds"
       << "With" << response.count << "Record";
    if (response.count != 1) {
      ss << "s";
    }
  }
  return ss.str();
}

TEST_P(GetAddrInfoTest, Basic) {
  const auto& [request, expectation] = GetParam();

  addrinfo* result;
  {
    const int ret = getaddrinfo(request.node, request.service, &request.hints, &result);

    if (expectation.is_error()) {
      ASSERT_EQ(ret, expectation.error_value())
          << "got: " << gai_strerror(ret) << "want: " << gai_strerror(expectation.error_value());
      return;
    }
    ASSERT_EQ(ret, 0) << gai_strerror(ret);
  }
  auto cleanup = fit::defer([result]() { freeaddrinfo(result); });
  const GetAddrInfoResponse& response = expectation.value();

  size_t i = 0;
  for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    const addrinfo& r = *rp;
    EXPECT_EQ(r.ai_flags, request.hints.ai_flags);
    EXPECT_EQ(r.ai_family, request.hints.ai_family);
    EXPECT_EQ(r.ai_socktype, request.hints.ai_socktype);
    switch (request.hints.ai_socktype) {
      case SOCK_STREAM: {
        EXPECT_EQ(r.ai_protocol, IPPROTO_TCP);
      } break;
      case SOCK_DGRAM: {
        EXPECT_EQ(r.ai_protocol, IPPROTO_UDP);
      } break;
      default:
        FAIL() << "unexpected ai_socktype: " << request.hints.ai_socktype;
    }

    switch (request.hints.ai_family) {
      case AF_INET: {
        ASSERT_EQ(r.ai_addrlen, sizeof(sockaddr_in));
        const auto& addr = *reinterpret_cast<const sockaddr_in*>(r.ai_addr);
        EXPECT_EQ(addr.sin_family, request.hints.ai_family);
        EXPECT_EQ(ntohs(addr.sin_port), response.port);

        char buf[INET_ADDRSTRLEN];
        ASSERT_NE(inet_ntop(r.ai_family, &addr.sin_addr, buf, sizeof(buf)), nullptr)
            << strerror(errno);

        EXPECT_STREQ(buf, response.address);
      } break;
      case AF_INET6: {
        ASSERT_EQ(r.ai_addrlen, sizeof(sockaddr_in6));
        const auto& addr = *reinterpret_cast<const sockaddr_in6*>(r.ai_addr);
        EXPECT_EQ(addr.sin6_family, request.hints.ai_family);
        EXPECT_EQ(ntohs(addr.sin6_port), response.port);

        char buf[INET6_ADDRSTRLEN];
        ASSERT_NE(inet_ntop(r.ai_family, &addr.sin6_addr, buf, sizeof(buf)), nullptr)
            << strerror(errno);

        EXPECT_STREQ(buf, response.address);
      } break;
      default:
        FAIL() << "unexpected ai_family: " << request.hints.ai_family;
    }

    EXPECT_STREQ(r.ai_canonname, request.node);

    i++;
  }
  EXPECT_EQ(i, response.count);
}

INSTANTIATE_TEST_SUITE_P(
    Basic, GetAddrInfoTest,
    testing::Values(std::make_tuple(GetAddrInfoRequest{.node = "example.com",
                                                       .service = "http",
                                                       .hints =
                                                           {
                                                               .ai_family = AF_INET,
                                                               .ai_socktype = SOCK_STREAM,
                                                           }},
                                    fit::ok(GetAddrInfoResponse{
                                        .port = 80,
                                        .address = "192.0.2.1",
                                        .count = 1,
                                    })),
                    std::make_tuple(GetAddrInfoRequest{.node = "example.com",
                                                       .service = "ntp",
                                                       .hints =
                                                           {
                                                               .ai_family = AF_INET6,
                                                               .ai_socktype = SOCK_DGRAM,
                                                           }},
                                    fit::ok(GetAddrInfoResponse{
                                        .port = 123,
                                        .address = "2001:db8::1",
                                        .count = 1,
                                    })),
                    std::make_tuple(GetAddrInfoRequest{.node = "lotsofrecords.com",
                                                       .service = "http",
                                                       .hints =
                                                           {
                                                               .ai_family = AF_INET,
                                                               .ai_socktype = SOCK_STREAM,
                                                           }},
                                    fit::ok(GetAddrInfoResponse{
                                        .port = 80,
                                        .address = "192.0.2.1",
                                        .count = MAXADDRS,
                                    })),
                    std::make_tuple(GetAddrInfoRequest{.node = "google.com",
                                                       .service = "http",
                                                       .hints =
                                                           {
                                                               .ai_family = AF_INET,
                                                               .ai_socktype = SOCK_STREAM,
                                                           }},
                                    fit::error(EAI_NONAME))),
    [](const testing::TestParamInfo<GetAddrInfoTest::ParamType>& info) {
      return to_string(info.param);
    });
