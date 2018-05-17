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
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
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
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
}

TEST(Linearizer, Push0_Push1_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());

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

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());

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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Pull(cb.NewPull());
  Mock::VerifyAndClearExpectations(&cb);

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("b"))))));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push1_Pull0_Push0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Pull(cb.NewPull());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
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
  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  EXPECT_CALL(
      cb, PullDone(Property(&StatusOr<Optional<Slice>>::get,
                            Pointee(Pointee(Slice::FromStaticString("a"))))));
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());
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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, false))).Times(2);
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("b")}, cb.NewPush());
  Mock::VerifyAndClearExpectations(&cb);

  EXPECT_CALL(cb, PullDone(Property(&StatusOr<Optional<Slice>>::is_ok, false)));
  linearizer.Pull(cb.NewPull());
}

TEST(Linearizer, Push1_Push01_Pull0_Pull1) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("ab")}, cb.NewPush());

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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("ab")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
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

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("bc")}, cb.NewPush());
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("ab")}, cb.NewPush());

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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("ab")}, cb.NewPush());
  linearizer.Push(Chunk{1, false, Slice::FromStaticString("bc")}, cb.NewPush());

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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("a")}, cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, false)));
  linearizer.Close(Status::Ok());
}

TEST(Linearizer, Push1_Push012_Pull0_Pull1_Pull2) {
  StrictMock<MockCallbacks> cb;
  Linearizer linearizer(128);

  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
  linearizer.Push(Chunk{0, false, Slice::FromStaticString("abc")},
                  cb.NewPush());

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

  linearizer.Push(Chunk{0, false, Slice::FromStaticString("abc")},
                  cb.NewPush());

  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  linearizer.Push(Chunk{1, false, Slice::FromStaticString("b")}, cb.NewPush());
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
  m.Push(0, 8, false, block0);
}

TEST(LinearizerFuzzed, _a3761115222a684a7e0ec40d2ddd601f3815dbb0) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  m.Push(0, 8, false, block0);
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
  m.Push(0, 97, false, block1);
}

TEST(LinearizerFuzzed, _7513024e0ab94a294598985655ca34ca4113f1f7) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Close(0);
  static const uint8_t block0[] = {0x00};
  m.Push(226, 1, false, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0xe2, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0xa9, 0x00};
  m.Push(0, 13, false, block1);
  m.Pull();
  static const uint8_t block2[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  m.Push(0, 30, false, block2);
}

TEST(LinearizerFuzzed, _399d8fb423eea52f471c33784d3f981a634355d5) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Close(0);
  static const uint8_t block0[] = {0x00};
  m.Push(226, 1, false, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0xe2, 0xc4, 0xc4, 0xc4,
                                   0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4};
  m.Push(10240, 13, false, block1);
  static const uint8_t block2[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block2);
  static const uint8_t block3[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block3);
  static const uint8_t block4[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block4);
  static const uint8_t block5[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block5);
  static const uint8_t block6[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block6);
  static const uint8_t block7[] = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};
  m.Push(3855, 13, false, block7);
  m.Pull();
}

TEST(LinearizerFuzzed, _dd43cb933e2c52c84b6d6f7279f6b1f49b9ecede) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  static const uint8_t block0[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
                                   0xff, 0xd2, 0xff, 0xff, 0xff, 0xff};
  m.Push(0, 12, false, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  m.Push(0, 8, false, block1);
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
  m.Push(2, 8, false, block0);
  m.Pull();
  static const uint8_t block1[] = {
      0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0xbf, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0xbf, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
  m.Push(0, 82, false, block1);
  m.Pull();
  m.Pull();
  m.Pull();
  m.Pull();
  static const uint8_t block2[] = {0x02};
  m.Push(514, 1, false, block2);
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
  m.Push(10, 244, false, block3);
}

TEST(LinearizerFuzzed, _a769cfc3afa79bf00b5dc4a4413dfe1690c9e41b) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0xff, 0xff, 0x20,
                                   0x10, 0x01, 0xfe, 0x20};
  m.Push(105, 8, false, block0);
  static const uint8_t block1[] = {0xf9, 0x03, 0x00, 0xff, 0x00, 0x0a, 0x00,
                                   0x01, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02};
  m.Push(510, 14, false, block1);
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
  m.Push(0, 82, false, block2);
  static const uint8_t block3[] = {0x00};
  m.Push(255, 1, false, block3);
  static const uint8_t block4[] = {0x01, 0x00, 0x02, 0x02,
                                   0x02, 0x02, 0x02, 0x02};
  m.Push(512, 8, false, block4);
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
  m.Push(0, 82, false, block5);
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
  m.Push(10, 244, false, block6);
}

// Caught off-by-one in ValidateInternals
TEST(LinearizerFuzzed, _d7ecc6236a8732fb747f193116229534abdad5d4) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {0x00, 0xff, 0xff, 0x20,
                                   0x00, 0x01, 0xfe, 0xf9};
  m.Push(105, 8, false, block0);
  static const uint8_t block1[] = {0x00};
  m.Push(33, 1, false, block1);
  static const uint8_t block2[] = {0x00, 0x00, 0xff, 0xff,
                                   0x03, 0x03, 0x03, 0x03};
  m.Push(255, 8, false, block2);
  static const uint8_t block3[] = {0x03};
  m.Push(771, 1, false, block3);
  static const uint8_t block4[] = {0x03};
  m.Push(771, 1, false, block4);
  static const uint8_t block5[] = {0x03};
  m.Push(771, 1, false, block5);
  static const uint8_t block6[] = {0x03};
  m.Push(771, 1, false, block6);
  static const uint8_t block7[] = {0x03};
  m.Push(771, 1, false, block7);
  static const uint8_t block8[] = {0x03};
  m.Push(771, 1, false, block8);
  static const uint8_t block9[] = {0x03};
  m.Push(771, 1, false, block9);
  static const uint8_t block10[] = {0x03};
  m.Push(771, 1, false, block10);
  static const uint8_t block11[] = {0x03};
  m.Push(771, 1, false, block11);
  static const uint8_t block12[] = {0x03};
  m.Push(771, 1, false, block12);
  static const uint8_t block13[] = {0x03};
  m.Push(771, 1, false, block13);
  static const uint8_t block14[] = {0x03};
  m.Push(771, 1, false, block14);
  static const uint8_t block15[] = {0x03};
  m.Push(771, 1, false, block15);
  static const uint8_t block16[] = {0x03};
  m.Push(771, 1, false, block16);
  static const uint8_t block17[] = {0x03};
  m.Push(771, 1, false, block17);
  static const uint8_t block18[] = {0x03};
  m.Push(771, 1, false, block18);
  static const uint8_t block19[] = {0x03};
  m.Push(771, 1, false, block19);
  static const uint8_t block20[] = {0x03};
  m.Push(771, 1, false, block20);
  static const uint8_t block21[] = {0x03};
  m.Push(771, 1, false, block21);
  static const uint8_t block22[] = {0x03};
  m.Push(771, 1, false, block22);
  static const uint8_t block23[] = {0x03};
  m.Push(771, 1, false, block23);
  static const uint8_t block24[] = {0x03};
  m.Push(771, 1, false, block24);
  static const uint8_t block25[] = {0x03};
  m.Push(771, 1, false, block25);
  static const uint8_t block26[] = {0x03};
  m.Push(771, 1, false, block26);
  static const uint8_t block27[] = {0x03};
  m.Push(771, 1, false, block27);
  static const uint8_t block28[] = {0x03};
  m.Push(771, 1, false, block28);
  static const uint8_t block29[] = {0x03};
  m.Push(771, 1, false, block29);
  static const uint8_t block30[] = {0x03};
  m.Push(771, 1, false, block30);
  static const uint8_t block31[] = {0x03};
  m.Push(771, 1, false, block31);
  static const uint8_t block32[] = {0x03};
  m.Push(771, 1, false, block32);
  static const uint8_t block33[] = {0x03};
  m.Push(771, 1, false, block33);
  static const uint8_t block34[] = {0x03};
  m.Push(771, 1, false, block34);
  static const uint8_t block35[] = {0x03};
  m.Push(771, 1, false, block35);
  static const uint8_t block36[] = {0x03};
  m.Push(771, 1, false, block36);
  static const uint8_t block37[] = {0x03};
  m.Push(771, 1, false, block37);
  static const uint8_t block38[] = {0x03};
  m.Push(771, 1, false, block38);
  static const uint8_t block39[] = {0x03};
  m.Push(771, 1, false, block39);
  static const uint8_t block40[] = {0x03};
  m.Push(771, 1, false, block40);
  static const uint8_t block41[] = {0x03};
  m.Push(771, 1, false, block41);
  static const uint8_t block42[] = {0x03};
  m.Push(771, 1, false, block42);
  static const uint8_t block43[] = {0x03};
  m.Push(771, 1, false, block43);
  static const uint8_t block44[] = {0x03};
  m.Push(771, 1, false, block44);
  static const uint8_t block45[] = {0xff};
  m.Push(1023, 1, false, block45);
  static const uint8_t block46[] = {
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03};
  m.Push(3, 30, false, block46);
  static const uint8_t block47[] = {0x03};
  m.Push(771, 1, false, block47);
  static const uint8_t block48[] = {0x03};
  m.Push(771, 1, false, block48);
  static const uint8_t block49[] = {0x03};
  m.Push(771, 1, false, block49);
  static const uint8_t block50[] = {0x20};
  m.Push(771, 1, false, block50);
}

// Exposed some growing pains in end-of-message handling.
TEST(LinearizerFuzzed, _ec62acf518af13e50b6accce7768ce52619cb9dd) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {};
  m.Push(8970, 0, true, block0);
  static const uint8_t block1[] = {};
  m.Push(0, 0, true, block1);
}

// Exposed some growing pains in end-of-message handling.
TEST(LinearizerFuzzed, _85e53271e14006f0265921d02d4d736cdc580b0b) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {
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
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  m.Push(0, 126, true, block0);
}

TEST(LinearizerFuzzed, _2215d90c8d9b57557cdd6c736ba44d5fd5b41869) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  static const uint8_t block0[] = {};
  m.Push(0, 0, true, block0);
}

TEST(LinearizerFuzzed, _a830b81a5fd181534360433a63f58974d52c2d4b) {
  linearizer_fuzzer::LinearizerFuzzer m;
  static const uint8_t block0[] = {};
  m.Push(0, 0, true, block0);
  static const uint8_t block1[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  m.Push(768, 18, true, block1);
}

TEST(LinearizerFuzzed, _4c631f1b91d2df163096fc841f1dcc70cd4c6070) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  static const uint8_t block0[] = {0x15, 0x02, 0x02, 0x02};
  m.Push(0, 4, true, block0);
  m.Pull();
  static const uint8_t block1[] = {};
  m.Push(514, 0, true, block1);
  static const uint8_t block2[] = {
      0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00};
  m.Push(768, 123, true, block2);
}

TEST(LinearizerFuzzed, _f343b14aef72b93b6f83247b96a2ab7637327bad) {
  linearizer_fuzzer::LinearizerFuzzer m;
  m.Pull();
  static const uint8_t block0[] = {
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x02, 0x02, 0x02, 0x02, 0x02,
      0x02, 0x04, 0x04, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x3b, 0x02, 0x00,
      0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x04, 0x04, 0x04, 0x04, 0x55,
      0x55, 0x55, 0x55, 0x55, 0x55, 0x05, 0x55, 0x55};
  m.Push(0, 41, false, block0);
  static const uint8_t block1[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0xab, 0xaa, 0xaa,
      0xaa, 0xaa, 0xaa, 0xaa, 0xaf, 0x55, 0x55, 0x55, 0x02, 0x02, 0x02,
      0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00,
      0x03, 0x00, 0x00, 0x01, 0x03, 0x03, 0x00, 0x00};
  m.Push(0, 41, true, block1);
  static const uint8_t block2[] = {};
  m.Push(0, 0, true, block2);
  // Snipped: crash happens before here.
}

}  // namespace linearizer_test
}  // namespace overnet
