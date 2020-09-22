// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "sequencer.h"

class TestSequencer {
 public:
  void Next() {
    uint32_t val = 5;
    auto sequencer = std::unique_ptr<Sequencer>(new Sequencer(val));
    EXPECT_EQ(sequencer->next_sequence_number(), val++);
    EXPECT_EQ(sequencer->next_sequence_number(), val++);
    EXPECT_EQ(sequencer->next_sequence_number(), val++);
    EXPECT_EQ(sequencer->next_sequence_number(), val++);
  }
};

TEST(Sequencer, Next) {
  TestSequencer test;
  test.Next();
}

TEST(Sequencer, Overflow) {
  // TODO(fxbug.dev/12695) - test overflow
}
