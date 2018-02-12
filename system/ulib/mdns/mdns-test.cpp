// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mdns/mdns.h>
#include <unittest/unittest.h>

static bool test_mdns_init_message(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);

  EXPECT_EQ(message.header.id, 0, "id should be zero");
  EXPECT_EQ(message.header.flags, 0, "flags should be zero");
  EXPECT_EQ(message.header.qd_count, 0, "question count should be zero");
  EXPECT_EQ(message.header.an_count, 0, "answer count should be zero");
  EXPECT_EQ(message.header.ns_count, 0, "name server count should be zero");
  EXPECT_EQ(message.header.ar_count, 0, "addition resource count should be zero");
  EXPECT_NULL(message.questions, "questions should be null");
  EXPECT_NULL(message.answers,  "answers should be null");
  EXPECT_NULL(message.authorities, "authorities should be null");
  EXPECT_NULL(message.additionals,  "additionals should be null");

  END_TEST;
}

BEGIN_TEST_CASE(mdns_init_message)
RUN_TEST(test_mdns_init_message)
END_TEST_CASE(mdns_init_message)

int main(int argc, char* argv[]) {
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
