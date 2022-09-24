// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/packet_view.h"

#include <lib/syslog/cpp/macros.h>

#include <ostream>
#include <string>
#include <vector>

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {
// Intersection Test cases are expressed with start+end, instead of start+count,
// so it's easier to visually see the intersection in each case.
struct IsectTestCase {
  Fixed packet_start;
  Fixed packet_end;
  Fixed range_start;
  Fixed range_end;
  bool want_isect;
  Fixed want_isect_start;
  Fixed want_isect_end;
  int64_t want_isect_payload_frame_offset;
};

// Some cases with integral packet boundaries.
std::vector<IsectTestCase> kIsectTestCasesIntegralBoundaries = {
    {
        // Range entirely before.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(0),
        .range_end = Fixed(10),
        .want_isect = false,
    },
    {
        // Range entirely after.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(20),
        .range_end = Fixed(30),
        .want_isect = false,
    },
    {
        // Range overlaps exactly.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(10),
        .range_end = Fixed(20),
        .want_isect = true,
        .want_isect_start = Fixed(10),
        .want_isect_end = Fixed(20),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Range overlaps first half.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(5),
        .range_end = Fixed(15),
        .want_isect = true,
        .want_isect_start = Fixed(10),
        .want_isect_end = Fixed(15),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Range overlaps second half.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(15),
        .range_end = Fixed(25),
        .want_isect = true,
        .want_isect_start = Fixed(15),
        .want_isect_end = Fixed(20),
        .want_isect_payload_frame_offset = 5,
    },
    {
        // Range within packet.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(12),
        .range_end = Fixed(17),
        .want_isect = true,
        .want_isect_start = Fixed(12),
        .want_isect_end = Fixed(17),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Range within packet, range is offset by max fraction.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(13) - Fixed::FromRaw(1),
        .range_end = Fixed(17) - Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(12),
        .want_isect_end = Fixed(16),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Range within packet, range is offset by min fraction.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(12) + Fixed::FromRaw(1),
        .range_end = Fixed(16) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(12),
        .want_isect_end = Fixed(16),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Range start outside packet by fractional amount.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(10) - Fixed::FromRaw(1),
        .range_end = Fixed(15) - Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(10),
        .want_isect_end = Fixed(14),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Range end outside packet by fractional amount.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(15) + Fixed::FromRaw(1),
        .range_end = Fixed(20) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(15),
        .want_isect_end = Fixed(20),
        .want_isect_payload_frame_offset = 5,
    },
    {
        // Range contains packet.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(5),
        .range_end = Fixed(25),
        .want_isect = true,
        .want_isect_start = Fixed(10),
        .want_isect_end = Fixed(20),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Range offset by min fraction and contains packet.
        .packet_start = Fixed(10),
        .packet_end = Fixed(20),
        .range_start = Fixed(5) + Fixed::FromRaw(1),
        .range_end = Fixed(25) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(10),
        .want_isect_end = Fixed(20),
        .want_isect_payload_frame_offset = 0,
    },
};

// Same as kIsectTestCasesIntegralBoundaries except packet_start and packet_end are fractional.
std::vector<IsectTestCase> kIsectTestCasesFractionalBoundaries = {
    {
        // Fractional packet: Range entirely before.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(0) + ffl::FromRatio(2, 4),
        .range_end = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect = false,
    },
    {
        // Fractional packet: Range entirely after.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(21) + ffl::FromRatio(2, 4),
        .range_end = Fixed(30) + ffl::FromRatio(2, 4),
        .want_isect = false,
    },
    {
        // Fractional packet: Range overlaps exactly.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(10) + ffl::FromRatio(2, 4),
        .range_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect = true,
        .want_isect_start = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Fractional packet: Range overlaps first half.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(5) + ffl::FromRatio(2, 4),
        .range_end = Fixed(15) + ffl::FromRatio(2, 4),
        .want_isect = true,
        .want_isect_start = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(15) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Fractional packet: Range overlaps second half.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(15) + ffl::FromRatio(2, 4),
        .range_end = Fixed(25) + ffl::FromRatio(2, 4),
        .want_isect = true,
        .want_isect_start = Fixed(15) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 5,
    },
    {
        // Fractional packet: Range within packet.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(12) + ffl::FromRatio(2, 4),
        .range_end = Fixed(17) + ffl::FromRatio(2, 4),
        .want_isect = true,
        .want_isect_start = Fixed(12) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(17) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Fractional packet: Range within packet, range is offset by max fraction.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(13) + ffl::FromRatio(2, 4) - Fixed::FromRaw(1),
        .range_end = Fixed(17) + ffl::FromRatio(2, 4) - Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(12) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(16) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Fractional packet: Range within packet, range is offset by min fraction.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(12) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .range_end = Fixed(16) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(12) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(16) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 2,
    },
    {
        // Fractional packet: Range start outside packet by fractional amount.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(10) + ffl::FromRatio(2, 4) - Fixed::FromRaw(1),
        .range_end = Fixed(15) + ffl::FromRatio(2, 4) - Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(14) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Fractional packet: Range end outside packet by fractional amount.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(15) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .range_end = Fixed(20) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(15) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 5,
    },
    {
        // Fractional packet: Range contains packet.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(5) + ffl::FromRatio(2, 4),
        .range_end = Fixed(25) + ffl::FromRatio(2, 4),
        .want_isect = true,
        .want_isect_start = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Fractional packet: Range offset by min fraction and contains packet.
        .packet_start = Fixed(10) + ffl::FromRatio(2, 4),
        .packet_end = Fixed(20) + ffl::FromRatio(2, 4),
        .range_start = Fixed(5) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .range_end = Fixed(25) + ffl::FromRatio(2, 4) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(10) + ffl::FromRatio(2, 4),
        .want_isect_end = Fixed(20) + ffl::FromRatio(2, 4),
        .want_isect_payload_frame_offset = 0,
    },
};

// Test cases that use negative frame positions.
std::vector<IsectTestCase> kIsectTestCasesNegativePositions = {
    {
        // Packet and range use negative numbers: range starts outside packet, ends inside
        .packet_start = Fixed(-10),
        .packet_end = Fixed(-5),
        .range_start = Fixed(-10) - Fixed::FromRaw(1),
        .range_end = Fixed(-5) - Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(-10),
        .want_isect_end = Fixed(-6),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Packet and range use negative numbers: range starts inside packet, ends outside
        .packet_start = Fixed(-10),
        .packet_end = Fixed(-5),
        .range_start = Fixed(-10) + Fixed::FromRaw(1),
        .range_end = Fixed(-5) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(-10),
        .want_isect_end = Fixed(-5),
        .want_isect_payload_frame_offset = 0,
    },
    {
        // Packet and range use negative numbers: range starts at first frame, ends outside
        .packet_start = Fixed(-10),
        .packet_end = Fixed(-5),
        .range_start = Fixed(-9) + Fixed::FromRaw(1),
        .range_end = Fixed(-4) + Fixed::FromRaw(1),
        .want_isect = true,
        .want_isect_start = Fixed(-9),
        .want_isect_end = Fixed(-5),
        .want_isect_payload_frame_offset = 1,
    },
};

// Test cases from API docs.
std::vector<IsectTestCase> kIsectTestCasesApiDocs = {
    {
        // Example #1 from API docs: everything integral
        .packet_start = Fixed(0),
        .packet_end = Fixed(10),
        .range_start = Fixed(1),
        .range_end = Fixed(3),
        .want_isect = true,
        .want_isect_start = Fixed(1),
        .want_isect_end = Fixed(3),
        .want_isect_payload_frame_offset = 1,
    },
    {
        // Example #2 from API docs: fractional offset range contained in integral offset packet
        .packet_start = Fixed(0),
        .packet_end = Fixed(10),
        .range_start = Fixed(1) + ffl::FromRatio(1, 2),
        .range_end = Fixed(3) + ffl::FromRatio(1, 2),
        .want_isect = true,
        .want_isect_start = Fixed(1),
        .want_isect_end = Fixed(3),
        .want_isect_payload_frame_offset = 1,
    },
    {
        // Example #3 from API docs: fractional offset range contained in fractional offset packet
        .packet_start = Fixed(0) + ffl::FromRatio(9, 10),
        .packet_end = Fixed(10) + ffl::FromRatio(9, 10),
        .range_start = Fixed(2) + ffl::FromRatio(1, 2),
        .range_end = Fixed(5) + ffl::FromRatio(1, 2),
        .want_isect = true,
        .want_isect_start = Fixed(1) + ffl::FromRatio(9, 10),
        .want_isect_end = Fixed(4) + ffl::FromRatio(9, 10),
        .want_isect_payload_frame_offset = 1,
    },
};

void RunIntersectionTests(const std::vector<IsectTestCase>& test_cases) {
  const auto format = Format::CreateOrDie({
      .sample_type = fuchsia_audio::SampleType::kInt16,
      .channels = 2,
      .frames_per_second = 48000,
  });

  for (auto& tc : test_cases) {
    SCOPED_TRACE(std::string("IntersectPacketView([") + ffl::String(tc.packet_start).c_str() +
                 ", " + ffl::String(tc.packet_end).c_str() + "), [" +
                 ffl::String(tc.range_start).c_str() + ", " + ffl::String(tc.range_end).c_str() +
                 "))");

    Fixed packet_length = tc.packet_end - tc.packet_start;
    ASSERT_EQ(packet_length.Fraction(), Fixed(0));

    Fixed range_length = tc.range_end - tc.range_start;
    ASSERT_EQ(range_length.Fraction(), Fixed(0));

    auto want_payload_offset_bytes = tc.want_isect_payload_frame_offset * format.bytes_per_frame();

    // Although we don't dereference packet_payload_buffer, UBSan requires that it point
    // to valid memory large enough to include the expected payload offset.
    std::vector<char> buffer(want_payload_offset_bytes + 1);
    char* const packet_payload_buffer = &buffer[0];

    PacketView packet({
        .format = format,
        .start = tc.packet_start,
        .length = packet_length.Floor(),
        .payload = packet_payload_buffer,
    });
    auto got = packet.IntersectionWith(tc.range_start, range_length.Floor());
    if (static_cast<bool>(got) != tc.want_isect) {
      ADD_FAILURE() << "got intersection = " << static_cast<bool>(got)
                    << ", want intersection = " << tc.want_isect;
      continue;
    }
    if (!tc.want_isect) {
      continue;
    }

    Fixed want_isect_length = tc.want_isect_end - tc.want_isect_start;
    ASSERT_EQ(want_isect_length.Fraction(), Fixed(0));

    if (got->start() != tc.want_isect_start || got->length() != want_isect_length.Floor() ||
        got->payload() != packet_payload_buffer + want_payload_offset_bytes) {
      ADD_FAILURE() << ffl::String::DecRational << "Unexpected result:\n"
                    << "got  = {.start = " << got->start()
                    << ", .end = " << Fixed(got->start() + Fixed(got->length()))
                    << ", .length = " << got->length() << ", .payload = " << got->payload() << ""
                    << "}\n"
                    << "want = {.start = " << tc.want_isect_start
                    << ", .end = " << tc.want_isect_end
                    << ", .length = " << want_isect_length.Floor()
                    << ", .payload = " << (void*)(packet_payload_buffer + want_payload_offset_bytes)
                    << "}\n";
    }
  }
}
}  // namespace

TEST(PacketViewTest, IntersectionWithIntegralBoundaries) {
  RunIntersectionTests(kIsectTestCasesIntegralBoundaries);
}

TEST(PacketViewTest, IntersectionWithFractionalBoundaries) {
  RunIntersectionTests(kIsectTestCasesFractionalBoundaries);
}

TEST(PacketViewTest, IntersectionWithNegativePositions) {
  RunIntersectionTests(kIsectTestCasesNegativePositions);
}

TEST(PacketViewTest, IntersectionWithApiDocs) { RunIntersectionTests(kIsectTestCasesApiDocs); }

TEST(PacketViewTest, Slice) {
  constexpr auto kBytesPerFrame = 4;
  const auto format = Format::CreateOrDie({
      .sample_type = fuchsia_audio::SampleType::kInt16,
      .channels = 2,
      .frames_per_second = 48000,
  });

  // Although we don't dereference packet_payload_buffer, UBSan requires that it point
  // to valid memory large enough to include the expected payload offset.
  std::vector<char> buffer(5 * format.bytes_per_frame());
  char* const packet_payload_buffer = &buffer[0];

  PacketView packet({
      .format = format,
      .start = Fixed(10),
      .length = 5,
      .payload = packet_payload_buffer,
  });

  struct TestCase {
    int64_t start_offset;
    int64_t end_offset;
    Fixed want_start;
    Fixed want_end;
    void* want_payload;
  };

  std::vector<TestCase> test_cases = {
      {
          // Entire packet
          .start_offset = 0,
          .end_offset = 5,
          .want_start = Fixed(10),
          .want_end = Fixed(15),
          .want_payload = packet_payload_buffer + 0 * kBytesPerFrame,
      },
      {
          // First frame only.
          .start_offset = 0,
          .end_offset = 1,
          .want_start = Fixed(10),
          .want_end = Fixed(11),
          .want_payload = packet_payload_buffer + 0 * kBytesPerFrame,
      },
      {
          // Last frame only.
          .start_offset = 4,
          .end_offset = 5,
          .want_start = Fixed(14),
          .want_end = Fixed(15),
          .want_payload = packet_payload_buffer + 4 * kBytesPerFrame,
      },
      {
          // Middle frames.
          .start_offset = 2,
          .end_offset = 4,
          .want_start = Fixed(12),
          .want_end = Fixed(14),
          .want_payload = packet_payload_buffer + 2 * kBytesPerFrame,
      },
  };

  for (auto& tc : test_cases) {
    std::ostringstream os;
    os << "Slice(" << tc.start_offset << ", " << tc.end_offset << ")";
    SCOPED_TRACE(os.str());

    auto got = packet.Slice(tc.start_offset, tc.end_offset);

    if (got.start() != tc.want_start || got.end() != tc.want_end ||
        got.payload() != tc.want_payload) {
      ADD_FAILURE() << ffl::String::DecRational << "Unexpected result:\n"
                    << "got  = {.start = " << got.start()                       //
                    << ", .end = " << Fixed(got.start() + Fixed(got.length()))  //
                    << ", .payload = " << got.payload()                         //
                    << "}\n"
                    << "want = {.start = " << tc.want_start  //
                    << ", .end = " << tc.want_end            //
                    << ", .payload = " << tc.want_payload    //
                    << "}\n";
    }
  }
}

}  // namespace media_audio
