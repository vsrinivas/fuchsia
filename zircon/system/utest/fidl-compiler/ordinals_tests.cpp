// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"

#define BORINGSSL_NO_CXX
#include <openssl/sha.h>

#include <regex>

#include <unittest/unittest.h>

namespace {

// Some of the tests below required generating strings offline until their
// SHA-256 sums had particular properties.  The code used to calculate a
// collision in the first 32 bits is included below, in case it proves useful in
// the future.

// #include <climits>
// #include <iostream>
// #include <openssl/sha.h>
// #include <stdio.h>
// #include <string.h>
// #include <string>

// std::string next_name(std::string& curr) {
//     std::string next = curr;
//     int i = next.length() - 1;
//     for (; i >= 0; i--) {
//         if (next[i] < 'z') {
//             next[i]++;
//             break;
//         } else {
//             next[i] = 'a';
//         }
//     }
//     if (i == -1) {
//         next = 'a' + next;
//     }
//     return next;
// }

// int main(int argc, char** argv) {
//     uint8_t* bitvec = new uint8_t[UINT_MAX];
//     std::string base("a.b/");
//     memset(bitvec, 0, UINT_MAX);
//     bool keep_going = true;
//     std::string curr_name = "a";
//     uint32_t ordinal = 0;
//     uint64_t iterations = 0;
//     do {
//         uint8_t digest[SHA256_DIGEST_LENGTH];
//         curr_name = next_name(curr_name);
//         std::string full_name = base + curr_name;
//         SHA256(reinterpret_cast<const uint8_t*>(full_name.data()), full_name.size(), digest);
//         ordinal = *(reinterpret_cast<uint32_t*>(digest)) & 0x7fffffff;
//         keep_going = bitvec[ordinal] == 0;
//         bitvec[ordinal] = 1;
//     } while (keep_going);
//     fprintf(stderr, "ordinal = %d name = %s\n", ordinal, curr_name.c_str());
// }

bool ordinal_cannot_be_zero() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library a;

// The first 32 bits of the SHA256 hash of a.b/fcuvhse are 0.
protocol b {
    fcuvhse() -> (int64 i);
};

)FIDL");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size(), "Ordinal value 0 should be disallowed");

    END_TEST;
}

bool clashing_ordinal_values() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library a;

// The first 32 bits of the SHA256 hash of a.b/ljz and a.b/clgn are
// the same.  This will trigger an error when ordinals are generated.
protocol b {
    ljz(string s, bool b) -> (int32 i);
    clgn(string s) -> (handle<channel> r);
};

)FIDL");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());

    // The FTP requires the error message as follows
    const std::regex pattern(R"REGEX(\[\s*Selector\s*=\s*"(ljz|clgn)_"\s*\])REGEX");
    std::smatch sm;
    ASSERT_TRUE(std::regex_search(errors[0], sm, pattern),
                ("Selector pattern not found in error: " + errors[0]).c_str());

    END_TEST;
}

bool clashing_ordinal_values_with_attribute() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library a;

// The first 32 bits of the SHA256 hash of a.b/ljz and a.b/clgn are
// the same.  This will trigger an error when ordinals are generated.
protocol b {
    [Selector = "ljz"]
    foo(string s, bool b) -> (int32 i);
    [Selector = "clgn"]
    bar(string s) -> (handle<channel> r);
};

)FIDL");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());

    // The FTP requires the error message as follows
    const std::regex pattern(R"REGEX(\[\s*Selector\s*=\s*"(ljz|clgn)_"\s*\])REGEX");
    std::smatch sm;
    ASSERT_TRUE(std::regex_search(errors[0], sm, pattern),
                ("Selector pattern not found in error: " + errors[0]).c_str());

    END_TEST;
}

bool attribute_resolves_clashes() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library a;

// The first 32 bits of the SHA256 hash of a.b/ljz and a.b/clgn are
// the same.  This will trigger an error when ordinals are generated.
protocol b {
    [Selector = "ljz_"]
    ljz(string s, bool b) -> (int32 i);
    clgn(string s) -> (handle<channel> r);
};

)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool ordinal_value_is_sha256() {
    BEGIN_TEST;
    TestLibrary library(R"FIDL(
library a;

protocol b {
    potato(string s, bool b) -> (int32 i);
};
)FIDL");
    ASSERT_TRUE(library.Compile());

    const char hash_name[] = "a.b/potato";
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(hash_name), strlen(hash_name), digest);
    uint32_t expected_hash = *(reinterpret_cast<uint32_t*>(digest)) & 0x7fffffff;

    const fidl::flat::Interface* iface = library.LookupInterface("b");
    uint32_t actual_hash = iface->methods[0].ordinal->value;
    ASSERT_EQ(actual_hash, expected_hash, "Expected hash is not correct");
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(ordinals_test)
RUN_TEST(ordinal_cannot_be_zero)
RUN_TEST(clashing_ordinal_values)
RUN_TEST(clashing_ordinal_values_with_attribute)
RUN_TEST(attribute_resolves_clashes)
RUN_TEST(ordinal_value_is_sha256)
END_TEST_CASE(ordinals_test)
