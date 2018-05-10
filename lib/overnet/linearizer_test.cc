// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linearizer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "linearizer_fuzzer_helpers.h"

using testing::InSequence;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::StrictMock;

namespace overnet {
namespace linearizer_test {

class MockCallbacks {
 public:
  MOCK_METHOD1(PushDone, void(const Status&));
  MOCK_METHOD1(PullDone, void(const StatusOr<Optional<Slice>>&));

  StatusCallback NewPush() {
    return StatusCallback(
        [this](const Status& status) { this->PushDone(status); });
  }

  StatusOrCallback<Optional<Slice>> NewPull() {
    return StatusOrCallback<Optional<Slice>>(
        [this](const StatusOr<Optional<Slice>>& status) {
          this->PullDone(status);
        });
  }
};

TEST(Linearizer, NoOp) { Linearizer(1024); }

TEST(Linearizer, Push0_Pull0) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  // Push at offset 0 then a Pull should complete immediately
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Pull0_Push0) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  // Push at offset 0 then a Pull should complete immediately
  linearizer.Pull(cb.NewPull());
  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
}

TEST(Linearizer, Push0_Push1_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push1_Push0_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push0_Pull0_Push1_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push1_Pull0_Push0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Pull(cb.NewPull());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Pull0_Push1_Push0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Pull(cb.NewPull());
  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push0_Push0_Pull0) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push0_PushBad0_Pull0) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, false))).Times(2);
  linearizer.Push(Chunk{0, Slice::FromStaticString("b")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PullDone(Property(&StatusOr<Optional<Slice>>::is_ok, false)));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push1_Push01_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, Slice::FromStaticString("ab")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push01_Push1_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("ab")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("ab"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push12_Push01_Pull0_Pull1_Pull2) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, Slice::FromStaticString("bc")}, cb.NewPush());
  linearizer.Push(Chunk{0, Slice::FromStaticString("ab")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("bc"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push01_Push12_Pull0_Pull1_Pull2) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("ab")}, cb.NewPush());
  linearizer.Push(Chunk{1, Slice::FromStaticString("bc")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("ab"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("c"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push_Close) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Close(Status::Ok());
}

TEST(Linearizer, Push1_Push012_Pull0_Pull1_Pull2) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, Slice::FromStaticString("abc")}, cb.NewPush());

  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("c"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push012_Push1_Pull0) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, Slice::FromStaticString("abc")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{1, Slice::FromStaticString("b")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("abc"))))));
  linearizer.Pull(cb.NewPull());
}

///////////////////////////////////////////////////////////////////////////////
// Fuzzer found failures
//
// Procedure: run the linearizer_fuzzer
//            use ./linearizer_fuzzer_corpus_to_code.py corpus_entry
//            copy paste test here, leave a comment about what failed
//            (also, fix it)

TEST(LinearizerFuzzed, _adc83b19e793491b1c6ea0fd8b46cd9f32e592fc) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  m.Push(0, 8, block0);
}

TEST(LinearizerFuzzed, _a3761115222a684a7e0ec40d2ddd601f3815dbb0) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  m.Push(0, 8, block0);
  static const uint8_t block1[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
      0x3a, 0x00, 0x00, 0x01, 0x10, 0xfe, 0xfe, 0x20, 0x00, 0x01, 0xfe,
      0xf9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  m.Push(0, 97, block1);
}

TEST(LinearizerFuzzed, _7513024e0ab94a294598985655ca34ca4113f1f7) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Close(0);
  static const uint8_t block0[] = {0x00};
  m.Push(226, 1, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0xa9, 0x00};
  m.Push(0, 13, block1);
  m.Pull();
  static const uint8_t block2[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  m.Push(0, 30, block2);
}

TEST(LinearizerFuzzed, _399d8fb423eea52f471c33784d3f981a634355d5) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Close(0);
  static const uint8_t block0[] = {0x00};
  m.Push(226, 1, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0xe2, 0xc4, 0xc4, 0xc4,
                                   0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4};
  m.Push(10240, 13, block1);
  static const uint8_t block2[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block2);
  static const uint8_t block3[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block3);
  static const uint8_t block4[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block4);
  static const uint8_t block5[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block5);
  static const uint8_t block6[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block6);
  static const uint8_t block7[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, block7);
  m.Pull();
}

TEST(LinearizerFuzzed, _dd43cb933e2c52c84b6d6f7279f6b1f49b9ecede) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  static const uint8_t block0[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
                                   0xff, 0xd2, 0xff, 0xff, 0xff, 0xff};
  m.Push(0, 12, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  m.Push(0, 8, block1);
}

TEST(LinearizerFuzzed, _946ff8cb484b160bf4e251212902c5e4b8b70f7b) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  m.Pull();
  m.Pull();
  m.Close(0);
}

// Crashed fuzzer helpers
TEST(LinearizerFuzzed, _025df41aed0b045a155f753a02991a564f5b7516) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x02, 0x02, 0x02, 0x02,
                                   0x02, 0x02, 0x02, 0x02};
  m.Push(2, 8, block0);
  m.Pull();
  static const uint8_t block1[] = {
      0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0xbf, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
  m.Push(0, 82, block1);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block2[] = {0x02};
  m.Push(514, 1, block2);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block3[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00};
  m.Push(10, 244, block3);
}

TEST(LinearizerFuzzed, _a769cfc3afa79bf00b5dc4a4413dfe1690c9e41b) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0xff, 0xff, 0x20,
                                   0x10, 0x01, 0xfe, 0x20};
  m.Push(105, 8, block0);
  static const uint8_t block1[] = {0xf9, 0x03, 0x00, 0xff, 0x00, 0x0a, 0x00,
                                   0x01, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02};
  m.Push(510, 14, block1);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block2[] = {
      0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0xfe, 0xf9,
      0x03, 0x00, 0xff, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x97, 0x02, 0x02, 0x02, 0x02, 0xf9};
  m.Push(0, 82, block2);
  static const uint8_t block3[] = {0x00};
  m.Push(255, 1, block3);
  static const uint8_t block4[] = {0x01, 0x00, 0x02, 0x02,
                                   0x02, 0x02, 0x02, 0x02};
  m.Push(512, 8, block4);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block5[] = {
      0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0xfe, 0xf9,
      0x03, 0x00, 0xff, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x97, 0x02, 0x02, 0x02, 0x02, 0x02};
  m.Push(0, 82, block5);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block6[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00};
  m.Push(10, 244, block6);
}

// Caught off-by-one in ValidateInternals
TEST(LinearizerFuzzed, _d7ecc6236a8732fb747f193116229534abdad5d4) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0xff, 0xff, 0x20,
                                   0x00, 0x01, 0xfe, 0xf9};
  m.Push(105, 8, block0);
  static const uint8_t block1[] = {0x00};
  m.Push(33, 1, block1);
  static const uint8_t block2[] = {0x00, 0x00, 0xff, 0xff,
                                   0x03, 0x03, 0x03, 0x03};
  m.Push(255, 8, block2);
  static const uint8_t block3[] = {0x03};
  m.Push(771, 1, block3);
  static const uint8_t block4[] = {0x03};
  m.Push(771, 1, block4);
  static const uint8_t block5[] = {0x03};
  m.Push(771, 1, block5);
  static const uint8_t block6[] = {0x03};
  m.Push(771, 1, block6);
  static const uint8_t block7[] = {0x03};
  m.Push(771, 1, block7);
  static const uint8_t block8[] = {0x03};
  m.Push(771, 1, block8);
  static const uint8_t block9[] = {0x03};
  m.Push(771, 1, block9);
  static const uint8_t block10[] = {0x03};
  m.Push(771, 1, block10);
  static const uint8_t block11[] = {0x03};
  m.Push(771, 1, block11);
  static const uint8_t block12[] = {0x03};
  m.Push(771, 1, block12);
  static const uint8_t block13[] = {0x03};
  m.Push(771, 1, block13);
  static const uint8_t block14[] = {0x03};
  m.Push(771, 1, block14);
  static const uint8_t block15[] = {0x03};
  m.Push(771, 1, block15);
  static const uint8_t block16[] = {0x03};
  m.Push(771, 1, block16);
  static const uint8_t block17[] = {0x03};
  m.Push(771, 1, block17);
  static const uint8_t block18[] = {0x03};
  m.Push(771, 1, block18);
  static const uint8_t block19[] = {0x03};
  m.Push(771, 1, block19);
  static const uint8_t block20[] = {0x03};
  m.Push(771, 1, block20);
  static const uint8_t block21[] = {0x03};
  m.Push(771, 1, block21);
  static const uint8_t block22[] = {0x03};
  m.Push(771, 1, block22);
  static const uint8_t block23[] = {0x03};
  m.Push(771, 1, block23);
  static const uint8_t block24[] = {0x03};
  m.Push(771, 1, block24);
  static const uint8_t block25[] = {0x03};
  m.Push(771, 1, block25);
  static const uint8_t block26[] = {0x03};
  m.Push(771, 1, block26);
  static const uint8_t block27[] = {0x03};
  m.Push(771, 1, block27);
  static const uint8_t block28[] = {0x03};
  m.Push(771, 1, block28);
  static const uint8_t block29[] = {0x03};
  m.Push(771, 1, block29);
  static const uint8_t block30[] = {0x03};
  m.Push(771, 1, block30);
  static const uint8_t block31[] = {0x03};
  m.Push(771, 1, block31);
  static const uint8_t block32[] = {0x03};
  m.Push(771, 1, block32);
  static const uint8_t block33[] = {0x03};
  m.Push(771, 1, block33);
  static const uint8_t block34[] = {0x03};
  m.Push(771, 1, block34);
  static const uint8_t block35[] = {0x03};
  m.Push(771, 1, block35);
  static const uint8_t block36[] = {0x03};
  m.Push(771, 1, block36);
  static const uint8_t block37[] = {0x03};
  m.Push(771, 1, block37);
  static const uint8_t block38[] = {0x03};
  m.Push(771, 1, block38);
  static const uint8_t block39[] = {0x03};
  m.Push(771, 1, block39);
  static const uint8_t block40[] = {0x03};
  m.Push(771, 1, block40);
  static const uint8_t block41[] = {0x03};
  m.Push(771, 1, block41);
  static const uint8_t block42[] = {0x03};
  m.Push(771, 1, block42);
  static const uint8_t block43[] = {0x03};
  m.Push(771, 1, block43);
  static const uint8_t block44[] = {0x03};
  m.Push(771, 1, block44);
  static const uint8_t block45[] = {0xff};
  m.Push(1023, 1, block45);
  static const uint8_t block46[] = {
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03};
  m.Push(3, 30, block46);
  static const uint8_t block47[] = {0x03};
  m.Push(771, 1, block47);
  static const uint8_t block48[] = {0x03};
  m.Push(771, 1, block48);
  static const uint8_t block49[] = {0x03};
  m.Push(771, 1, block49);
  static const uint8_t block50[] = {0x20};
  m.Push(771, 1, block50);
}

}  // namespace linearizer_test
}  // namespace overnet
