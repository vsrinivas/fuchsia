// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"

#define BORINGSSL_NO_CXX
#include <openssl/sha.h>
#include <unittest/unittest.h>

#include <regex>

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
library a.b.c;

protocol protocol {
    selector(string s, bool b) -> (int32 i);
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  const char hash_name32[] = "a.b.c.protocol/selector";
  uint8_t digest32[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(hash_name32), strlen(hash_name32), digest32);
  uint64_t expected_hash32 = *(reinterpret_cast<uint64_t*>(digest32)) & 0x7fffffff;

  const char hash_name64[] = "a.b.c/protocol.selector";
  uint8_t digest64[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(hash_name64), strlen(hash_name64), digest64);
  uint64_t expected_hash64 = *(reinterpret_cast<uint64_t*>(digest64)) & 0x7fffffffffffffff;

  const fidl::flat::Protocol* iface = library.LookupProtocol("protocol");
  uint64_t actual_hash32 = iface->methods[0].generated_ordinal32->value;
  ASSERT_EQ(actual_hash32, expected_hash32, "Expected 32bits hash is not correct");
  uint64_t actual_hash64 = iface->methods[0].generated_ordinal64->value;
  ASSERT_EQ(actual_hash64, expected_hash64, "Expected 64bits hash is not correct");
  END_TEST;
}

// generated by gen_ordinal_value_is_first64bits_of_sha256_test.sh
bool ordinal_value_is_first64bits_of_sha256() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library a.b.c;

protocol protocol {
    s0();
    s1();
    s2();
    s3();
    s4();
    s5();
    s6();
    s7();
    s8();
    s9();
    s10();
    s11();
    s12();
    s13();
    s14();
    s15();
    s16();
    s17();
    s18();
    s19();
    s20();
    s21();
    s22();
    s23();
    s24();
    s25();
    s26();
    s27();
    s28();
    s29();
    s30();
    s31();
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  const fidl::flat::Protocol* iface = library.LookupProtocol("protocol");
  EXPECT_EQ(iface->methods[0].generated_ordinal64->value, 0x3b1625372e15f1ae);
  EXPECT_EQ(iface->methods[1].generated_ordinal64->value, 0x4199e504fa71b5a4);
  EXPECT_EQ(iface->methods[2].generated_ordinal64->value, 0x247ca8a890628135);
  EXPECT_EQ(iface->methods[3].generated_ordinal64->value, 0x64f7c02cfffb7846);
  EXPECT_EQ(iface->methods[4].generated_ordinal64->value, 0x20d3f06c598f0cc3);
  EXPECT_EQ(iface->methods[5].generated_ordinal64->value, 0x1ce13806085dac7a);
  EXPECT_EQ(iface->methods[6].generated_ordinal64->value, 0x09e1d4b200770def);
  EXPECT_EQ(iface->methods[7].generated_ordinal64->value, 0x53df65d26411d8ee);
  EXPECT_EQ(iface->methods[8].generated_ordinal64->value, 0x690c3617405590c7);
  EXPECT_EQ(iface->methods[9].generated_ordinal64->value, 0x4ff9ef5fb170f550);
  EXPECT_EQ(iface->methods[10].generated_ordinal64->value, 0x1542d4c21d8a6c00);
  EXPECT_EQ(iface->methods[11].generated_ordinal64->value, 0x564e9e47f7418e0f);
  EXPECT_EQ(iface->methods[12].generated_ordinal64->value, 0x29681e66f3506231);
  EXPECT_EQ(iface->methods[13].generated_ordinal64->value, 0x5ee63b26268f7760);
  EXPECT_EQ(iface->methods[14].generated_ordinal64->value, 0x256950edf00aac63);
  EXPECT_EQ(iface->methods[15].generated_ordinal64->value, 0x6b21c0ff1aa02896);
  EXPECT_EQ(iface->methods[16].generated_ordinal64->value, 0x5a54f3dca00089e9);
  EXPECT_EQ(iface->methods[17].generated_ordinal64->value, 0x772476706fa4be0e);
  EXPECT_EQ(iface->methods[18].generated_ordinal64->value, 0x294e338bf71a773b);
  EXPECT_EQ(iface->methods[19].generated_ordinal64->value, 0x5a6aa228cfb68d16);
  EXPECT_EQ(iface->methods[20].generated_ordinal64->value, 0x55a09c6b033f3f98);
  EXPECT_EQ(iface->methods[21].generated_ordinal64->value, 0x1192d5b856d22cd8);
  EXPECT_EQ(iface->methods[22].generated_ordinal64->value, 0x2e68bdea28f9ce7b);
  EXPECT_EQ(iface->methods[23].generated_ordinal64->value, 0x4c8ebf26900e4451);
  EXPECT_EQ(iface->methods[24].generated_ordinal64->value, 0x3df0dbe9378c4fd3);
  EXPECT_EQ(iface->methods[25].generated_ordinal64->value, 0x087268657bb0cad1);
  EXPECT_EQ(iface->methods[26].generated_ordinal64->value, 0x0aee6ad161a90ae1);
  EXPECT_EQ(iface->methods[27].generated_ordinal64->value, 0x44e6f2282baf727a);
  EXPECT_EQ(iface->methods[28].generated_ordinal64->value, 0x3e8984f57ab5830d);
  EXPECT_EQ(iface->methods[29].generated_ordinal64->value, 0x696f9f73a5cabd21);
  EXPECT_EQ(iface->methods[30].generated_ordinal64->value, 0x327d7b0d2389e054);
  EXPECT_EQ(iface->methods[31].generated_ordinal64->value, 0x54fd307bb5bfab2d);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(ordinals_test)
RUN_TEST(ordinal_cannot_be_zero)
RUN_TEST(clashing_ordinal_values)
RUN_TEST(clashing_ordinal_values_with_attribute)
RUN_TEST(attribute_resolves_clashes)
RUN_TEST(ordinal_value_is_sha256)
RUN_TEST(ordinal_value_is_first64bits_of_sha256)
END_TEST_CASE(ordinals_test)
