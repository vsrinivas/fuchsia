// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mdns/mdns.h>

#include <errno.h>
#include <unittest/unittest.h>

// Test values.
const uint8_t kRdata[4] = {0xA, 0xB, 0xC};
const char kRrName[] = "test_rr";

// Test state manages sample values for testing. It is always initialized the
// same way.
//
// TODO(kjharland): Rector existing tests to use this and reduce boilerplate.
struct test_data {
    bool reset() {
        // Init message.
        mdns_init_message(&message);

        // Init rr.
        strcpy(rr.name, kRrName);
        rr.type = RR_TYPE_AAAA;
        rr.clazz = RR_CLASS_IN;
        rr.rdata = (uint8_t*)kRdata;
        rr.rdlength = sizeof(rr.rdata) / sizeof(uint8_t);
        rr.ttl = 42;
        return true;
    }

    mdns_message message;
    mdns_rr rr;
};

// Returns true iff the given question has the given expected properties.
static bool verify_question(mdns_question* q, char* domain, uint16_t qtype, uint16_t qclass) {
    ASSERT_STR_EQ(q->domain, domain, "question has incorrect domain");
    ASSERT_EQ(q->qtype, qtype, "question has incorrect type");
    ASSERT_EQ(q->qclass, qclass, "question has incorrect class");
    return true;
}

// Returns true iff the given resource record has the given expected properties.
static bool verify_rr(mdns_rr* rr,
                      char* name,
                      uint16_t type,
                      uint16_t clazz,
                      uint8_t* rdata,
                      uint16_t rdlength,
                      uint32_t ttl) {
    ASSERT_STR_EQ(rr->name, name, "rr has incorrect name");
    ASSERT_EQ(rr->type, type, "rr has incorrect type");
    ASSERT_EQ(rr->clazz, clazz, "rr has incorrect class");
    ASSERT_EQ(rr->rdlength, rdlength, "rr has incorrect rdlength");
    ASSERT_EQ(rr->ttl, ttl, "rr has incorrect ttl");
    return true;
}

// Returns true iff the given message is zeroed out.
static bool verify_message_is_zeroed(mdns_message* message) {
    ASSERT_EQ(message->header.id, 0, "id should be zero");
    ASSERT_EQ(message->header.flags, 0, "flags should be zero");
    ASSERT_EQ(message->header.qd_count, 0, "question count should be zero");
    ASSERT_EQ(message->header.an_count, 0, "answer count should be zero");
    ASSERT_EQ(message->header.ns_count, 0, "name server count should be zero");
    ASSERT_EQ(message->header.ar_count, 0, "addition resource count should be zero");
    ASSERT_NULL(message->questions, "questions should be null");
    ASSERT_NULL(message->answers, "answers should be null");
    ASSERT_NULL(message->authorities, "authorities should be null");
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

    char domain[MAX_DOMAIN_LENGTH + 1];
    memset(domain, 1, MAX_DOMAIN_LENGTH + 1);

    int retval = mdns_add_question(&message, domain, qtype, qclass);
    EXPECT_EQ(retval, -1, "should return -1 on error");
    EXPECT_EQ(errno, ENAMETOOLONG, "errno should be ENAMETOOLONG");
    EXPECT_NULL(message.questions, "question should not have been added on error");

    mdns_free_message(&message);

    END_TEST;
}

static bool test_mdns_add_first_answer(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_answer(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                 t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");
    EXPECT_NONNULL(t.message.answers, "answer was not added");
    EXPECT_EQ(t.message.header.an_count, 1, "answer count should be one");
    EXPECT_TRUE(verify_rr(t.message.answers, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    END_TEST;
}

static bool test_mdns_add_nth_answer(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_answer(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                 t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");

    char other_name[] = "other name";
    uint16_t other_type = RR_TYPE_A;
    uint16_t other_clazz = RR_CLASS_IN;
    uint8_t other_rdata[] = {t.rr.rdata[0]};
    uint16_t other_rdlength = sizeof(other_rdata) / sizeof(uint8_t);
    uint32_t other_ttl = t.rr.ttl + 1;
    retval = mdns_add_answer(&t.message, other_name, other_type, other_clazz,
                             other_rdata, other_rdlength, other_ttl);
    EXPECT_NONNULL(t.message.answers, "answer was not added");
    EXPECT_EQ(t.message.header.an_count, 2, "answer count should be two");

    EXPECT_TRUE(verify_rr(t.message.answers, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    EXPECT_NONNULL(t.message.answers->next, "second answer was not added");
    EXPECT_TRUE(verify_rr(t.message.answers->next, other_name, other_type,
                          other_clazz, other_rdata, other_rdlength, other_ttl));
    EXPECT_NULL(t.message.answers->next->next,
                "second answer nextptr should be null");

    END_TEST;
}

static bool test_mdns_add_answer_bad_rr_type(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.type = (uint16_t)(RR_TYPE_A + 1); // Unsupported record type.
    int retval = mdns_add_answer(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                 t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr type");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.answers, "should not have added answer to message");
    EXPECT_EQ(t.message.header.an_count, 0, "answer count should be zero");

    END_TEST;
}

static bool test_mdns_add_answer_bad_rr_class(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.clazz = (uint16_t)(RR_CLASS_IN + 1); // Unsupported record class.
    int retval = mdns_add_answer(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                 t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr class");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.answers, "should not have added answer to message");
    EXPECT_EQ(t.message.header.an_count, 0, "answer count should be zero");

    END_TEST;
}

static bool test_mdns_add_first_authority(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_authority(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                    t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");
    EXPECT_NONNULL(t.message.authorities, "authority was not added");
    EXPECT_EQ(t.message.header.ns_count, 1, "authority count should be one");
    EXPECT_TRUE(verify_rr(t.message.authorities, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    END_TEST;
}

static bool test_mdns_add_nth_authority(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_authority(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                    t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");

    char other_name[] = "other name";
    uint16_t other_type = RR_TYPE_A;
    uint16_t other_clazz = RR_CLASS_IN;
    uint8_t other_rdata[] = {t.rr.rdata[0]};
    uint16_t other_rdlength = sizeof(other_rdata) / sizeof(uint8_t);
    uint32_t other_ttl = t.rr.ttl + 1;
    retval = mdns_add_authority(&t.message, other_name, other_type, other_clazz,
                                other_rdata, other_rdlength, other_ttl);
    EXPECT_NONNULL(t.message.authorities, "authority was not added");
    EXPECT_EQ(t.message.header.ns_count, 2, "authority count should be two");

    EXPECT_TRUE(verify_rr(t.message.authorities, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    EXPECT_NONNULL(t.message.authorities->next, "second authority was not added");
    EXPECT_TRUE(verify_rr(t.message.authorities->next, other_name, other_type,
                          other_clazz, other_rdata, other_rdlength, other_ttl));
    EXPECT_NULL(t.message.authorities->next->next,
                "second authority nextptr should be null");

    END_TEST;
}

static bool test_mdns_add_authority_bad_rr_type(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.type = (uint16_t)(RR_TYPE_A + 1); // Unsupported record type.
    int retval = mdns_add_authority(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                    t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr type");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.authorities, "should not have added authority to message");
    EXPECT_EQ(t.message.header.ns_count, 0, "authority count should be zero");

    END_TEST;
}

static bool test_mdns_add_authority_bad_rr_class(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.clazz = (uint16_t)(RR_CLASS_IN + 1); // Unsupported record class.
    int retval = mdns_add_authority(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                    t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr class");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.authorities, "should not have added authority to message");
    EXPECT_EQ(t.message.header.ns_count, 0, "authority count should be zero");

    END_TEST;
}

static bool test_mdns_add_first_additional(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_additional(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                     t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");
    EXPECT_NONNULL(t.message.additionals, "additional was not added");
    EXPECT_EQ(t.message.header.ar_count, 1, "additional count should be one");
    EXPECT_TRUE(verify_rr(t.message.additionals, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    END_TEST;
}

static bool test_mdns_add_nth_additional(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();

    int retval = mdns_add_additional(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                     t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(retval, 0, "should return zero if no error");

    char other_name[] = "other name";
    uint16_t other_type = RR_TYPE_A;
    uint16_t other_clazz = RR_CLASS_IN;
    uint8_t other_rdata[] = {t.rr.rdata[0]};
    uint16_t other_rdlength = sizeof(other_rdata) / sizeof(uint8_t);
    uint32_t other_ttl = t.rr.ttl + 1;
    retval = mdns_add_additional(&t.message, other_name, other_type, other_clazz,
                                 other_rdata, other_rdlength, other_ttl);
    EXPECT_NONNULL(t.message.additionals, "additional was not added");
    EXPECT_EQ(t.message.header.ar_count, 2, "additional count should be two");

    EXPECT_TRUE(verify_rr(t.message.additionals, t.rr.name, t.rr.type, t.rr.clazz,
                          t.rr.rdata, t.rr.rdlength, t.rr.ttl));

    EXPECT_NONNULL(t.message.additionals->next, "second additional was not added");
    EXPECT_TRUE(verify_rr(t.message.additionals->next, other_name, other_type,
                          other_clazz, other_rdata, other_rdlength, other_ttl));
    EXPECT_NULL(t.message.additionals->next->next,
                "second additional nextptr should be null");

    END_TEST;
}

static bool test_mdns_add_additional_bad_rr_type(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.type = (uint16_t)(RR_TYPE_A + 1); // Unsupported record type.
    int retval = mdns_add_additional(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                     t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr type");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.additionals, "should not have added additional to message");
    EXPECT_EQ(t.message.header.ar_count, 0, "additional count should be zero");

    END_TEST;
}

static bool test_mdns_add_additional_bad_rr_class(void) {
    BEGIN_TEST;

    test_data t;
    t.reset();
    t.rr.clazz = (uint16_t)(RR_CLASS_IN + 1); // Unsupported record class.
    int retval = mdns_add_additional(&t.message, t.rr.name, t.rr.type, t.rr.clazz,
                                     t.rr.rdata, t.rr.rdlength, t.rr.ttl);
    EXPECT_EQ(errno, EINVAL, "errno should be EINVAL when given bad rr class");
    EXPECT_EQ(retval, -1, "should return value < zero on error");
    EXPECT_NULL(t.message.additionals, "should not have added additional to message");
    EXPECT_EQ(t.message.header.ar_count, 0, "additional count should be zero");

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

static bool test_mdns_unmarshal_incomplete_header(void) {
    BEGIN_TEST;

    mdns_message message;
    uint16_t encoded_message[] = {0};

    // pass buf_len value smaller than MDNS_HEADER_SIZE to indicate the full
    // message did not fit into the provided buffer.
    int retval = mdns_unmarshal(encoded_message, MDNS_HEADER_SIZE - 1, &message);
    EXPECT_LT(retval, 0, "should have returned a negative value on error");
    EXPECT_TRUE(verify_message_is_zeroed(&message),
                "message was mutated even though decoding failed");

    retval = mdns_unmarshal(encoded_message, MDNS_HEADER_SIZE - 5, &message);
    EXPECT_LT(retval, 0, "should have returned a negative value on error");
    EXPECT_TRUE(verify_message_is_zeroed(&message),
                "message was mutated even though decoding failed");

    retval = mdns_unmarshal(encoded_message, 0, &message);
    EXPECT_LT(retval, 0, "should have returned a negative value on error");
    EXPECT_TRUE(verify_message_is_zeroed(&message),
                "message was mutated even though decoding failed");

    END_TEST;
}

static bool test_mdns_unmarshal_empty_message(void) {
    BEGIN_TEST;

    mdns_message message;

    // Completely empty message.
    uint16_t encoded_message_1[] = {
        // Header section
        0, // ID
        0, // Flags section
        0, // Question count
        0, // Answer count
        0, // Authority count
        0, // Additionals count
        // No message content.
    };

    int retval = mdns_unmarshal(encoded_message_1, MDNS_HEADER_SIZE, &message);
    EXPECT_EQ(retval, MDNS_HEADER_SIZE, "should have read 12 bytes");
    EXPECT_TRUE(verify_message_is_zeroed(&message),
                "message is not zeroed even though input data was empty");

    // Message with ID and flags but still "empty" because no questions or
    // records are inside.
    uint16_t encoded_message_2[] = {
        // Header section
        0xABCD, // ID
        0xCDEF, // Flags section
        0,      // Question count
        0,      // Answer count
        0,      // Authority count
        0,      // Additionals count
        // No message content.
    };

    retval = mdns_unmarshal(encoded_message_2, MDNS_HEADER_SIZE, &message);
    EXPECT_EQ(retval, MDNS_HEADER_SIZE, "should have read 12 bytes");
    EXPECT_EQ(message.header.id, 0xABCD, "ID should be 0xABCD (171)");
    EXPECT_EQ(message.header.flags, 0xCDEF, "flags should be 0xCDEF (205)");
    EXPECT_EQ(message.header.qd_count, 0, "question count should be 0");
    EXPECT_EQ(message.header.qd_count, 0, "answer count should be 0");
    EXPECT_EQ(message.header.qd_count, 0, "authority count should be 0");
    EXPECT_EQ(message.header.qd_count, 0, "additionals count should be 0");
    EXPECT_NULL(message.questions, "questions should be null");
    EXPECT_NULL(message.answers, "answers should be null");
    EXPECT_NULL(message.authorities, "authorities should be null");
    EXPECT_NULL(message.additionals, "additionals should be null");
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

BEGIN_TEST_CASE(mdns_add_answer)
RUN_TEST(test_mdns_add_first_answer)
RUN_TEST(test_mdns_add_nth_answer)
RUN_TEST(test_mdns_add_answer_bad_rr_type)
RUN_TEST(test_mdns_add_answer_bad_rr_class)
END_TEST_CASE(mdns_add_answer)

BEGIN_TEST_CASE(mdns_add_authority)
RUN_TEST(test_mdns_add_first_authority)
RUN_TEST(test_mdns_add_nth_authority)
RUN_TEST(test_mdns_add_authority_bad_rr_type)
RUN_TEST(test_mdns_add_authority_bad_rr_class)
END_TEST_CASE(mdns_add_authority)

BEGIN_TEST_CASE(mdns_add_additional)
RUN_TEST(test_mdns_add_first_additional)
RUN_TEST(test_mdns_add_nth_additional)
RUN_TEST(test_mdns_add_additional_bad_rr_type)
RUN_TEST(test_mdns_add_additional_bad_rr_class)
END_TEST_CASE(mdns_add_additional)

BEGIN_TEST_CASE(test_mdns_unmarshal)
RUN_TEST(test_mdns_unmarshal_incomplete_header)
RUN_TEST(test_mdns_unmarshal_empty_message)
END_TEST_CASE(test_mdns_unmarshal)

int main(int argc, char* argv[]) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
