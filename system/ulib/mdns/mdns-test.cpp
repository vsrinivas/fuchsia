// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mdns/mdns.h>

#include <unittest/unittest.h>
#include <errno.h>

// Returns true iff the given question's domain, qtype and qclass match the given values.
static bool verify_question(mdns_question* q, char* domain, uint16_t qtype, uint16_t qclass) {
  EXPECT_STR_EQ(q->domain, domain, strlen(domain), "question has incorrect domain");
  EXPECT_EQ(q->qtype, qtype, "question has incorrect type");
  EXPECT_EQ(q->qclass, qclass, "question has incorrect class");
  return true;
}

static bool verify_message_is_zeroed(mdns_message* message) {
  EXPECT_EQ(message->header.id, 0, "id should be zero");
  EXPECT_EQ(message->header.flags, 0, "flags should be zero");
  EXPECT_EQ(message->header.qd_count, 0, "question count should be zero");
  EXPECT_EQ(message->header.an_count, 0, "answer count should be zero");
  EXPECT_EQ(message->header.ns_count, 0, "name server count should be zero");
  EXPECT_EQ(message->header.ar_count, 0, "addition resource count should be zero");
  EXPECT_NULL(message->questions, "questions should be null");
  EXPECT_NULL(message->answers,  "answers should be null");
  EXPECT_NULL(message->authorities, "authorities should be null");
  return true;
}

static bool test_mdns_init_message(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);
  EXPECT_TRUE(verify_message_is_zeroed(&message), "message was not zeroed");
  mdns_free_message(&message);

  END_TEST;
}

static bool test_mdns_add_first_question(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);
  EXPECT_EQ(message.header.qd_count, 0, "question count should be zero");
  EXPECT_NULL(message.questions, "questions should be null");

  char domain[20] = "https://fuchsia.com";
  uint16_t qtype = 0x1234;
  uint16_t qclass = 0xABCD;

  int retval = mdns_add_question(&message, domain, qtype, qclass);
  EXPECT_EQ(retval, 0, "should return zero if no error");
  EXPECT_EQ(message.header.qd_count, 1, "question count should be one");
  EXPECT_TRUE(verify_question(message.questions, domain, qtype, qclass));
  EXPECT_NULL(message.questions->next, "last question next ptr should be NULL");

  mdns_free_message(&message);

  END_TEST;
}

static bool test_mdns_add_nth_question(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);
  EXPECT_EQ(message.header.qd_count, 0, "question count should be zero");
  EXPECT_NULL(message.questions, "questions should be null");

  char domain[20] = "https://fuchsia.com";
  uint16_t qtypeA = 0x1234;
  uint16_t qclassA = 0xABCD;

  int retval = mdns_add_question(&message, domain, qtypeA, qclassA);
  EXPECT_EQ(retval, 0, "should return zero if no error");

  message.header.qd_count = 4; // Fiddle with header to ensure it's reset.
  uint16_t qtypeB = 0x1235;
  uint16_t qclassB = 0xABCE;
  retval = mdns_add_question(&message, domain, qtypeB, qclassB);
  EXPECT_EQ(retval, 0, "should return zero if no error");

  EXPECT_EQ(message.header.qd_count, 2, "question count should be two");
  EXPECT_TRUE(verify_question(message.questions, domain, qtypeA, qclassA));
  EXPECT_NONNULL(message.questions->next, "non-last question next ptr should not be NULL");
  EXPECT_TRUE(verify_question(message.questions->next, domain, qtypeB, qclassB));
  EXPECT_NULL(message.questions->next->next, "last question next ptr should be NULL");

  mdns_free_message(&message);

  END_TEST;
}

static bool test_mdns_add_domain_too_long(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);

  uint16_t qtype = 0x1234;
  uint16_t qclass = 0xABCD;

  char domain[MAX_DOMAIN_LENGTH+1];
  memset(domain, 1, MAX_DOMAIN_LENGTH+1);

  int retval = mdns_add_question(&message, domain, qtype, qclass);
  EXPECT_EQ(retval, -1, "should return -1 on error");
  EXPECT_EQ(errno, ENAMETOOLONG, "errno should be ENAMETOOLONG");
  EXPECT_NULL(message.questions, "question should not have been added on error");

  mdns_free_message(&message);

  END_TEST;
}

static bool test_mdns_free_message(void) {
  BEGIN_TEST;

  mdns_message message;
  mdns_init_message(&message);

  char domain[20] = "https://fuchsia.com";
  int retval = mdns_add_question(&message, domain, 0, 0);
  EXPECT_EQ(retval, 0, "should return zero if no error");
  retval = mdns_add_question(&message, domain, 0, 0);
  EXPECT_EQ(retval, 0, "should return zero if no error");

  // Double check questions were successfully added.
  EXPECT_NONNULL(message.questions, "first question was not added");
  EXPECT_NONNULL(message.questions->next, "second question was not added");

  mdns_free_message(&message);
  EXPECT_TRUE(verify_message_is_zeroed(&message), "message was not zeroed");

  END_TEST;
}

BEGIN_TEST_CASE(mdns_free_message)
RUN_TEST(test_mdns_free_message)
END_TEST_CASE(mdns_free_message)

BEGIN_TEST_CASE(mdns_init_message)
RUN_TEST(test_mdns_init_message)
END_TEST_CASE(mdns_init_message)

BEGIN_TEST_CASE(mdns_add_question)
RUN_TEST(test_mdns_add_first_question)
RUN_TEST(test_mdns_add_nth_question)
RUN_TEST(test_mdns_add_domain_too_long)
END_TEST_CASE(mdns_add_question)

int main(int argc, char* argv[]) {
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
