// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/intersect.h"

#include <gtest/gtest.h>

namespace media::audio::mixer {

namespace {
// Test cases are expressed with start+end, instead of start+count,
// so it's easier to visually see the intersection in each case.
struct TestCase {
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
std::vector<TestCase> kTestCasesIntegralBoundaries = {
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

// Same as kTestCasesIntegralBoundaries except packet_start and packet_end are fractional.
std::vector<TestCase> kTestCasesFractionalBoundaries = {
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
std::vector<TestCase> kTestCasesNegativePositions = {
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
std::vector<TestCase> kTestCasesApiDocs = {
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
}  // namespace

void RunTests(const std::vector<TestCase>& test_cases) {
  const auto format =
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value();

  for (auto& tc : test_cases) {
    SCOPED_TRACE(std::string("IntersectPacket([") + ffl::String(tc.packet_start).c_str() + ", " +
                 ffl::String(tc.packet_end).c_str() + "), [" + ffl::String(tc.range_start).c_str() +
                 ", " + ffl::String(tc.range_end).c_str() + "))");

    Fixed packet_length = tc.packet_end - tc.packet_start;
    ASSERT_EQ(packet_length.Fraction(), Fixed(0));

    Fixed range_length = tc.range_end - tc.range_start;
    ASSERT_EQ(range_length.Fraction(), Fixed(0));

    auto want_payload_offset_bytes = tc.want_isect_payload_frame_offset * format.bytes_per_frame();

    // Although we don't dereference packet_payload_buffer, UBSan requires that it point
    // to valid memory large enough to include the expected payload offset.
    std::vector<char> buffer(want_payload_offset_bytes + 1);
    char* const packet_payload_buffer = &buffer[0];

    Packet packet{
        .start = tc.packet_start,
        .length = packet_length.Floor(),
        .payload = packet_payload_buffer,
    };
    auto got = IntersectPacket(format, packet, tc.range_start, range_length.Floor());
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

    if (got->start != tc.want_isect_start || got->length != want_isect_length.Floor() ||
        got->payload != packet_payload_buffer + want_payload_offset_bytes) {
      ADD_FAILURE() << ffl::String::DecRational << "Unexpected result:\n"
                    << "got  = {.start = " << got->start
                    << ", .end = " << Fixed(got->start + Fixed(got->length))
                    << ", .length = " << got->length << ", .payload = " << (void*)got->payload
                    << "}\n"
                    << "want = {.start = " << tc.want_isect_start
                    << ", .end = " << tc.want_isect_end
                    << ", .length = " << want_isect_length.Floor()
                    << ", .payload = " << (void*)(packet_payload_buffer + want_payload_offset_bytes)
                    << "}\n";
    }
  }
}

TEST(IntersectTest, IntegralBoundaries) { RunTests(kTestCasesIntegralBoundaries); }

TEST(IntersectTest, FractionalBoundaries) { RunTests(kTestCasesFractionalBoundaries); }

TEST(IntersectTest, NegativePositions) { RunTests(kTestCasesNegativePositions); }

TEST(IntersectTest, ApiDocs) { RunTests(kTestCasesApiDocs); }

}  // namespace media::audio::mixer
