// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame.h"

#include <zxtest/zxtest.h>

#include <vector>

#include "fcs.h"

namespace {

TEST(FrameTestCase, SerializeFrameEmpty) {
  const std::vector<uint8_t> information = {};
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0xc2, 0x23, 0xeb, 0x3c, 0x7e,
  };

  const ppp::FrameView frame(ppp::Protocol::ChallengeHandshakeAuthentication, information);
  const std::vector<uint8_t> raw_frame = ppp::SerializeFrame(frame);

  ASSERT_EQ(raw_frame.size(), expect.size());
  ASSERT_BYTES_EQ(raw_frame.data(), expect.data(), expect.size());
}

TEST(FrameTestCase, SerializeFrameNoEscape) {
  const std::vector<uint8_t> information = {0x89, 0xab, 0xde};
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0xc0, 0x23, 0x89, 0xab, 0xde, 0x7d, 0x37, 0x7d, 0x2b, 0x7e,
  };

  const ppp::FrameView frame(ppp::Protocol::PasswordAuthentication, information);
  const std::vector<uint8_t> raw_frame = ppp::SerializeFrame(frame);

  ASSERT_EQ(raw_frame.size(), expect.size());
  ASSERT_BYTES_EQ(raw_frame.data(), expect.data(), expect.size());
}

TEST(FrameTestCase, SerializeFrameEscapeFlagSequence) {
  const std::vector<uint8_t> information = {0xaa, 0x7e, 0xaa};
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x80, 0x57, 0xaa, 0x7d, 0x5e, 0xaa, 0xe3, 0x7d, 0x3a, 0x7e,
  };

  const ppp::FrameView frame(ppp::Protocol::Ipv6Control, information);
  const std::vector<uint8_t> raw_frame = ppp::SerializeFrame(frame);

  ASSERT_EQ(raw_frame.size(), expect.size());
  ASSERT_BYTES_EQ(raw_frame.data(), expect.data(), expect.size());
}

TEST(FrameTestCase, SerializeFrameEscapeEscaped) {
  const std::vector<uint8_t> information = {0x7d, 0x5e};
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 0x7d, 0x5d, 0x5e, 0xc9, 0xb5, 0x7e,
  };

  const ppp::FrameView frame(ppp::Protocol::Ipv4, information);
  const std::vector<uint8_t> raw_frame = ppp::SerializeFrame(frame);

  ASSERT_EQ(raw_frame.size(), expect.size());
  ASSERT_BYTES_EQ(raw_frame.data(), expect.data(), expect.size());
}

TEST(FrameTestCase, SerializeFrameEscapeAll) {
  const std::vector<uint8_t> information = {0x7e, 0x7e, 0x7e, 0x7d, 0x7d, 0x7d};
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0xc0, 0x25, 0x7d, 0x5e, 0x7d, 0x5e, 0x7d,
      0x5e, 0x7d, 0x5d, 0x7d, 0x5d, 0x7d, 0x5d, 0x6c, 0x43, 0x7e,
  };

  const ppp::FrameView frame(ppp::Protocol::LinkQualityReport, information);
  const std::vector<uint8_t> raw_frame = ppp::SerializeFrame(frame);

  ASSERT_EQ(raw_frame.size(), expect.size());
  ASSERT_BYTES_EQ(raw_frame.data(), expect.data(), expect.size());
}

TEST(FrameTestCase, DeserializeFrameEmpty) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc2, 0x23, 0xeb, 0x3c, 0x7e,
  };
  const std::vector<uint8_t> expect_information = {};
  const ppp::Protocol expect_protocol = ppp::Protocol::ChallengeHandshakeAuthentication;

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_ok());

  const auto frame = result.take_value();
  ASSERT_EQ(frame.protocol, expect_protocol);

  ASSERT_EQ(frame.information.size(), expect_information.size());
  ASSERT_BYTES_EQ(frame.information.data(), expect_information.data(), expect_information.size());
}

TEST(FrameTestCase, DeserializeFrameNoEscape) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc0, 0x23, 0x89, 0xab, 0xde, 0x7d, 0x37, 0x7d, 0x2b, 0x7e,
  };
  const std::vector<uint8_t> expect_information = {0x89, 0xab, 0xde};
  const ppp::Protocol expect_protocol = ppp::Protocol::PasswordAuthentication;

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_ok());

  const auto frame = result.take_value();
  ASSERT_EQ(frame.protocol, expect_protocol);

  ASSERT_EQ(frame.information.size(), expect_information.size());
  ASSERT_BYTES_EQ(frame.information.data(), expect_information.data(), expect_information.size());
}

TEST(FrameTestCase, DeserializeFrameEscapeFlagSequence) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0x80, 0x57, 0xaa, 0x7d, 0x5e, 0xaa, 0xe3, 0x7d, 0x3a, 0x7e,
  };
  const std::vector<uint8_t> expect_information = {0xaa, 0x7e, 0xaa};
  const ppp::Protocol expect_protocol = ppp::Protocol::Ipv6Control;

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_ok());

  const auto frame = result.take_value();
  ASSERT_EQ(frame.protocol, expect_protocol);

  ASSERT_EQ(frame.information.size(), expect_information.size());
  ASSERT_BYTES_EQ(frame.information.data(), expect_information.data(), expect_information.size());
}

TEST(FrameTestCase, DeserializeFrameEscapeEscaped) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 0x7d, 0x5d, 0x5e, 0xc9, 0xb5, 0x7e,
  };
  const std::vector<uint8_t> expect_information = {0x7d, 0x5e};
  const ppp::Protocol expect_protocol = ppp::Protocol::Ipv4;

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_ok());

  const auto frame = result.take_value();
  ASSERT_EQ(frame.protocol, expect_protocol);

  ASSERT_EQ(frame.information.size(), expect_information.size());
  ASSERT_BYTES_EQ(frame.information.data(), expect_information.data(), expect_information.size());
}

TEST(FrameTestCase, DeserializeFrameEscapeAll) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc0, 0x25, 0x7d, 0x5e, 0x7d, 0x5e, 0x7d,
      0x5e, 0x7d, 0x5d, 0x7d, 0x5d, 0x7d, 0x5d, 0x6c, 0x43, 0x7e,
  };
  const std::vector<uint8_t> expect_information = {0x7e, 0x7e, 0x7e, 0x7d, 0x7d, 0x7d};
  const ppp::Protocol expect_protocol = ppp::Protocol::LinkQualityReport;

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_ok());

  const auto frame = result.take_value();
  ASSERT_EQ(frame.protocol, expect_protocol);

  ASSERT_EQ(frame.information.size(), expect_information.size());
  ASSERT_BYTES_EQ(frame.information.data(), expect_information.data(), expect_information.size());
}

TEST(FrameTestCase, DeserializeFrameTooSmall) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc2, 0xeb, 0x3c, 0x7e,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::FormatInvalid);
}

TEST(FrameTestCase, DeserializeFrameMissingOpenFlag) {
  const std::vector<uint8_t> raw_frame = {
      0xff, 0x7d, 0x23, 0xc2, 0x23, 0xaa, 0xeb, 0x3c, 0x7e,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::FormatInvalid);
}

TEST(FrameTestCase, DeserializeFrameMissingCloseFlag) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc2, 0x23, 0xaa, 0xeb, 0x3c,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::FormatInvalid);
}

TEST(FrameTestCase, DeserializeFrameBadAddress) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0x7d, 0x20, 0x7d, 0x23, 0xc2, 0x23, 0xeb, 0x3c, 0x7e,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::UnrecognizedAddress);
}

TEST(FrameTestCase, DeserializeFrameBadControl) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x20, 0xc2, 0x23, 0xeb, 0x3c, 0x7e,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::UnrecognizedControl);
}

TEST(FrameTestCase, DeserializeFrameBadFrameCheckSequence) {
  const std::vector<uint8_t> raw_frame = {
      0x7e, 0xff, 0x7d, 0x23, 0xc2, 0x23, 0x7d, 0x20, 0x7d, 0x20, 0x7e,
  };

  auto result = ppp::DeserializeFrame(raw_frame);

  ASSERT_TRUE(result.is_error());

  ASSERT_EQ(result.error(), ppp::DeserializationError::FailedFrameCheckSequence);
}

}  // namespace
