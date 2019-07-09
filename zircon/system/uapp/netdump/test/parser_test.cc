// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include <lib/mock-function/mock-function.h>
#include <zxtest/zxtest.h>

#include <iostream>
#include <sstream>

namespace netdump::parser::test {

// The `FilterBuilder` that builds nothing but calls a mock function for each of the operations.
// The "filter type" is a `size_t` so the order of mock function calls can be identified by the
// return value.
class MockFilterBuilder : public FilterBuilder<size_t> {
 public:
  // Conditionally call the mock functions if `call_mocks_` is true.
  template <typename... Args>
  size_t call_mock(mock_function::MockFunction<size_t, Args...>* fn, Args... args) {
    if (call_mocks_) {
      return fn->Call(args...);
    }
    return 0;
  }

  mock_function::MockFunction<size_t, uint16_t, TokenPtr> frame_length_mock;
  size_t frame_length(uint16_t length, TokenPtr comparator) override {
    return call_mock(&frame_length_mock, length, comparator);
  }

  mock_function::MockFunction<size_t, uint16_t> ethertype_mock;
  size_t ethertype(uint16_t type) override { return call_mock(&ethertype_mock, type); }

  mock_function::MockFunction<size_t, std::array<uint8_t, ETH_ALEN>, TokenPtr> mac_mock;
  size_t mac(std::array<uint8_t, ETH_ALEN> address, TokenPtr addr_type) override {
    return call_mock(&mac_mock, address, addr_type);
  }

  mock_function::MockFunction<size_t, uint8_t> ip_version_mock;
  size_t ip_version(uint8_t version) override { return call_mock(&ip_version_mock, version); }

  mock_function::MockFunction<size_t, uint8_t, uint16_t, TokenPtr> ip_pkt_length_mock;
  size_t ip_pkt_length(uint8_t version, uint16_t length, TokenPtr comparator) override {
    return call_mock(&ip_pkt_length_mock, version, length, comparator);
  }

  mock_function::MockFunction<size_t, uint8_t, uint8_t> ip_protocol_mock;
  size_t ip_protocol(uint8_t version, uint8_t protocol) override {
    return call_mock(&ip_protocol_mock, version, protocol);
  }

  mock_function::MockFunction<size_t, uint32_t, TokenPtr> ipv4_address_mock;
  size_t ipv4_address(uint32_t address, TokenPtr type) override {
    return call_mock(&ipv4_address_mock, address, type);
  }

  mock_function::MockFunction<size_t, std::array<uint8_t, IP6_ADDR_LEN>, TokenPtr>
      ipv6_address_mock;
  size_t ipv6_address(std::array<uint8_t, IP6_ADDR_LEN> address, TokenPtr addr_type) override {
    return call_mock(&ipv6_address_mock, address, addr_type);
  }

  mock_function::MockFunction<size_t, std::vector<std::pair<uint16_t, uint16_t>>, TokenPtr>
      ports_mock;
  size_t ports(std::vector<std::pair<uint16_t, uint16_t>> ranges, TokenPtr port_type) override {
    return call_mock(&ports_mock, ranges, port_type);
  }

  mock_function::MockFunction<size_t, size_t> negation_mock;
  size_t negation(size_t filter) override { return call_mock(&negation_mock, filter); }

  mock_function::MockFunction<size_t, size_t, size_t> conjunction_mock;
  size_t conjunction(size_t left, size_t right) override {
    return call_mock(&conjunction_mock, left, right);
  }

  mock_function::MockFunction<size_t, size_t, size_t> disjunction_mock;
  size_t disjunction(size_t left, size_t right) override {
    return call_mock(&disjunction_mock, left, right);
  }

  explicit MockFilterBuilder(const Tokenizer& tokenizer) : FilterBuilder<size_t>(tokenizer) {}

#define NETDUMP_APPLY_TO_ALL_MOCKS(fn) \
  do {                                 \
    frame_length_mock.fn();            \
    ethertype_mock.fn();               \
    mac_mock.fn();                     \
    ip_version_mock.fn();              \
    ip_pkt_length_mock.fn();           \
    ip_protocol_mock.fn();             \
    ipv4_address_mock.fn();            \
    ipv6_address_mock.fn();            \
    ports_mock.fn();                   \
    negation_mock.fn();                \
    conjunction_mock.fn();             \
    disjunction_mock.fn();             \
  } while (false)

  inline void verify_and_clear_all() { NETDUMP_APPLY_TO_ALL_MOCKS(VerifyAndClear); }

  inline void expect_no_call_all() { NETDUMP_APPLY_TO_ALL_MOCKS(ExpectNoCall); }
#undef NETDUMP_APPLY_TO_ALL_MOCKS

  void stop_call_mocks() { call_mocks_ = false; }

 private:
  bool call_mocks_ = true;
};

static const Tokenizer TKZ{};  // One tokenizer for all the tests.

// Test token cursor transitions in the parse environment.
TEST(NetdumpParserTest, EnvironmentPlusPlusTest) {
  Environment env(std::vector<TokenPtr>{TKZ.PORT, TKZ.HOST});

  EXPECT_EQ(TKZ.PORT, *env);
  ++env;
  EXPECT_EQ(TKZ.HOST, *env);
}

TEST(NetdumpParserTest, EnvironmentMinusMinusTest) {
  Environment env(std::vector<TokenPtr>{TKZ.TCP, TKZ.IP6});

  EXPECT_EQ(TKZ.TCP, *env);
  ++env;
  EXPECT_EQ(TKZ.IP6, *env);
  --env;
  EXPECT_EQ(TKZ.TCP, *env);
}

TEST(NetdumpParserTest, EnvironmentGuardsTest) {
  Environment env(std::vector<TokenPtr>{TKZ.ICMP, TKZ.ARP});

  EXPECT_EQ(TKZ.ICMP, *env);
  --env;
  EXPECT_EQ(TKZ.ICMP, *env);
  ++env;
  ++env;
  EXPECT_TRUE(env.at_end());
  ++env;
  EXPECT_TRUE(env.at_end());
  --env;
  EXPECT_EQ(TKZ.ARP, *env);
}

TEST(NetdumpParserTest, EnvironmentEndDereferenceTest) {
  Environment env(std::vector<TokenPtr>{TKZ.ICMP});
  ++env;

  EXPECT_TRUE(env.at_end());
  ASSERT_DEATH([&env]() { *env; });
}

TEST(NetdumpParserTest, EnvironmentFullWalkTest) {
  Environment env(std::vector<TokenPtr>{TKZ.AND, TKZ.DNS, TKZ.DHCP, TKZ.SRC});

  EXPECT_EQ(env.begin(), env.cur());
  EXPECT_EQ(TKZ.AND, *env);
  ++env;
  EXPECT_EQ(TKZ.DNS, *env);
  --env;
  EXPECT_EQ(TKZ.AND, *env);
  --env;
  EXPECT_EQ(TKZ.AND, *env);
  ++env;
  ++env;
  EXPECT_EQ(TKZ.DHCP, *env);
  ++env;
  EXPECT_FALSE(env.at_end());
  EXPECT_EQ(TKZ.SRC, *env);
  ++env;
  EXPECT_EQ(env.end(), env.cur());
  ++env;
  EXPECT_TRUE(env.at_end());
  --env;
  EXPECT_EQ(TKZ.SRC, *env);

  EXPECT_FALSE(env.has_error());
  env.error_loc = env.cur();
  env.error_cause = "cause";

  env.reset();
  EXPECT_EQ(TKZ.AND, *env);

  EXPECT_TRUE(env.has_error());
  env.clear_error();
  EXPECT_EQ(std::nullopt, env.error_loc);
  EXPECT_EQ("", env.error_cause);
}

using MockParseResult = std::variant<size_t, ParseError>;

class TestParser : public Parser {
 public:
  void HighlightErrorTest() {
    Environment env(std::vector<TokenPtr>{TKZ.AND, TKZ.DNS, TKZ.DHCP});

    env.error_loc = std::nullopt;
    // Just returns the string if no error location.
    // The use of EXPECT_STR_EQ (which only allows C-strings) gives much nicer debug messages
    // than EXPECT_EQ between `std::string`.
    EXPECT_STR_EQ("spec", highlight_error("spec", &env).c_str());

    std::stringstream expect_string1;
    env.error_loc = env.end();
    expect_string1 << "spec" << ANSI_HIGHLIGHT_ERROR << "*" << ANSI_RESET;
    // Reproduce spec string and append error marker if error location is at end.
    EXPECT_STR_EQ(expect_string1.str().c_str(), highlight_error("spec", &env).c_str());

    std::stringstream expect_string2;
    env.error_loc = env.begin();
    expect_string2 << ANSI_HIGHLIGHT_ERROR << TKZ.AND->get_term() << ANSI_RESET << " "
                   << TKZ.DNS->get_term() << " " << TKZ.DHCP->get_term();
    EXPECT_STR_EQ(expect_string2.str().c_str(), highlight_error("spec", &env).c_str());

    std::stringstream expect_string3;
    env.reset();
    ++env;
    env.error_loc = env.cur();
    --env;  // This Test error is highlighted by error location, not `env` state.
    expect_string3 << TKZ.AND->get_term() << " " << ANSI_HIGHLIGHT_ERROR << TKZ.DNS->get_term()
                   << ANSI_RESET << " " << TKZ.DHCP->get_term();
    EXPECT_STR_EQ(expect_string3.str().c_str(), highlight_error("spec", &env).c_str());

    std::stringstream expect_string4;
    env.reset();
    ++env;
    ++env;
    env.error_loc = env.cur();
    expect_string4 << TKZ.AND->get_term() << " " << TKZ.DNS->get_term() << " "
                   << ANSI_HIGHLIGHT_ERROR << TKZ.DHCP->get_term() << ANSI_RESET;
    EXPECT_STR_EQ(expect_string4.str().c_str(), highlight_error("spec", &env).c_str());
  }

  // Syntax logic tests.
  void UnknownKeywordTest() {
    Environment env = TestEnv("mumble jumble");
    ExpectError(parse(&env, &bld_));
    // The error cause is `ERROR_UNKNOWN_KEYWORD`.
    EXPECT_STR_EQ(ERROR_UNKNOWN_KEYWORD, env.error_cause.c_str());
    // The string where the error occurred was `mumble`.
    EXPECT_STR_EQ("mumble", (**env.error_loc)->get_term().c_str());
    // Invalid filter string with no known keywords should end up with no filter operation calls
    // after parsing.
    bld_.verify_and_clear_all();
  }

  // Each individual type of expression is tested, with both success and error cases.
  void FrameLengthTest() {
    // Mock function arguments are (out, in...)
    // The out value can be used to identify the result of different calls.
    // Use {} for don't-care out value.
    bld_.frame_length_mock.ExpectCall({}, 100, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall({}, 50, TKZ.LESS);

    ExpectSuccess(parse("greater 100", &bld_));
    ExpectSuccess(parse("less 50", &bld_));
    bld_.verify_and_clear_all();

    // Do not track `FilterBuilder` function calls for error cases as the parser may partially
    // construct the filter before failing.
    bld_.stop_call_mocks();
    Environment env = TestEnv("less -100");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_INVALID_LENGTH, env.error_cause.c_str());
    EXPECT_STR_EQ("-100", (**env.error_loc)->get_term().c_str());
  }

  void NotTest() {
    bld_.frame_length_mock.ExpectCall(0, 100, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall(1, 50, TKZ.LESS);
    // Apply negation to filter returned by the first `frame_length_mock` call.
    bld_.negation_mock.ExpectCall({}, 0);

    ExpectSuccess(parse("not greater 100", &bld_));
    ExpectSuccess(parse("not not less 50", &bld_));
    bld_.verify_and_clear_all();

    bld_.stop_call_mocks();
    Environment env = TestEnv("not not not");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_CONNECTIVE, env.error_cause.c_str());
    EXPECT_EQ(env.end() - 1, env.error_loc);  // Error location: "not not *not*"
  }

  void CompositionTest() {
    bld_.frame_length_mock.ExpectCall(0, 100, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall(1, 50, TKZ.LESS);
    bld_.frame_length_mock.ExpectCall(2, 60, TKZ.LESS);
    bld_.frame_length_mock.ExpectCall(3, 200, TKZ.GREATER);
    // Ensure the `frame_length_mock` calls are paired together appropriately.
    bld_.conjunction_mock.ExpectCall({}, 0, 1);
    bld_.disjunction_mock.ExpectCall({}, 2, 3);

    ExpectSuccess(parse("greater 100 and less 50", &bld_));
    ExpectSuccess(parse("less 60 or greater 200", &bld_));
    bld_.verify_and_clear_all();

    bld_.stop_call_mocks();
    Environment env = TestEnv("less 25 and or greater 100");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_CONNECTIVE, env.error_cause.c_str());
    EXPECT_STR_EQ("or", (**env.error_loc)->get_term().c_str());

    env = TestEnv("less 25 greater 100");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_REQUIRED_CONNECTIVE, env.error_cause.c_str());
    EXPECT_STR_EQ("greater", (**env.error_loc)->get_term().c_str());
  }

  void ParenthesisTest() {
    bld_.frame_length_mock.ExpectCall(0, 10, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall(1, 11, TKZ.LESS);
    bld_.frame_length_mock.ExpectCall(2, 12, TKZ.LESS);
    bld_.frame_length_mock.ExpectCall(3, 13, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall(4, 14, TKZ.GREATER);
    bld_.frame_length_mock.ExpectCall(5, 15, TKZ.LESS);
    bld_.frame_length_mock.ExpectCall(6, 16, TKZ.LESS);
    // Ordering of logical operations must be correct.
    // Without parenthesis, association is to the left.
    bld_.conjunction_mock.ExpectCall(10, 0, 1);
    bld_.disjunction_mock.ExpectCall({}, 10, 2);
    bld_.conjunction_mock.ExpectCall(20, 4, 5);
    bld_.disjunction_mock.ExpectCall({}, 3, 20);

    ExpectSuccess(parse("greater 10 and less 11 or less 12", &bld_));
    ExpectSuccess(parse("greater 13 or ( greater 14 and less 15 )", &bld_));
    ExpectSuccess(parse("( less 16 )", &bld_));
    bld_.verify_and_clear_all();

    bld_.stop_call_mocks();
    Environment env = TestEnv("( less 25 and ( greater 100 or ( greater 200 ) ) ) )");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_R_PARENS, env.error_cause.c_str());
    EXPECT_EQ(env.end() - 1, env.error_loc);  // Error on last ")".

    env = TestEnv("less 25 or ( greater 100");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNMATCHED_L_PARENS, env.error_cause.c_str());
    EXPECT_STR_EQ("(", (**env.error_loc)->get_term().c_str());

    env = TestEnv("less 25 ( greater 100 )");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_REQUIRED_CONNECTIVE, env.error_cause.c_str());
    EXPECT_STR_EQ("(", (**env.error_loc)->get_term().c_str());

    env = TestEnv("(");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNMATCHED_L_PARENS, env.error_cause.c_str());
    EXPECT_STR_EQ("(", (**env.error_loc)->get_term().c_str());

    env = TestEnv(")");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_R_PARENS, env.error_cause.c_str());
    EXPECT_STR_EQ(")", (**env.error_loc)->get_term().c_str());

    env = TestEnv("( ) ( ) ( ) ( ( ) ( ) ( ( ) ( ( ) ) ( ) ( ) )");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_R_PARENS, env.error_cause.c_str());
    EXPECT_EQ(env.begin() + 1, env.error_loc);  // Error on second token, i.e. first ")".

    env = TestEnv("( ( ( ( ( ) ) ) ) )");
    ExpectError(parse(&env, &bld_));
    EXPECT_STR_EQ(ERROR_UNEXPECTED_R_PARENS, env.error_cause.c_str());
    EXPECT_EQ(env.begin() + 5, env.error_loc);  // Error on first ")".
  }

  explicit TestParser() : Parser(TKZ), bld_(TKZ) { bld_.expect_no_call_all(); }

 private:
  inline Environment TestEnv(const std::string& filter_spec) {
    return Environment(TKZ.tokenize(filter_spec));
  }

  inline void ExpectSuccess(MockParseResult filter) {
    EXPECT_TRUE(std::holds_alternative<size_t>(filter));
  }

  inline void ExpectSuccess(std::optional<size_t> filter) { EXPECT_NE(filter, std::nullopt); }

  inline void ExpectError(MockParseResult filter) {
    EXPECT_TRUE(std::holds_alternative<ParseError>(filter));
  }

  inline void ExpectError(std::optional<size_t> filter) { EXPECT_EQ(filter, std::nullopt); }

  MockFilterBuilder bld_;
};

#define NETDUMP_TEST(test) \
  TEST(NetdumpParserTest, test) { TestParser().test(); }

NETDUMP_TEST(HighlightErrorTest)
NETDUMP_TEST(UnknownKeywordTest)
NETDUMP_TEST(FrameLengthTest)
NETDUMP_TEST(NotTest)
NETDUMP_TEST(CompositionTest)
NETDUMP_TEST(ParenthesisTest)

#undef NETDUMP_TEST

}  // namespace netdump::parser::test
