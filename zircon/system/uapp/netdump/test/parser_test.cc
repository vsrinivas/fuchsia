// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include <iostream>
#include <sstream>

#include <lib/mock-function/mock-function.h>
#include <zxtest/zxtest.h>

namespace netdump::parser::test {

// The `FilterBuilder` that builds nothing but calls a mock function for each of the operations.
class MockFilterBuilder : public FilterBuilder<std::monostate> {
public:
    mock_function::MockFunction<std::monostate, uint16_t, TokenPtr> frame_length_mock;
    std::monostate frame_length(uint16_t length, TokenPtr comparator) override {
        return frame_length_mock.Call(length, comparator);
    }

    mock_function::MockFunction<std::monostate, uint16_t> ethertype_mock;
    std::monostate ethertype(uint16_t type) override {
        return ethertype_mock.Call(type);
    }

    mock_function::MockFunction<std::monostate, std::array<uint8_t, ETH_ALEN>, TokenPtr>
        mac_mock;
    std::monostate mac(std::array<uint8_t, ETH_ALEN> address, TokenPtr addr_type) override {
        return mac_mock.Call(address, addr_type);
    }

    mock_function::MockFunction<std::monostate, uint8_t> ip_version_mock;
    std::monostate ip_version(uint8_t version) override {
        return ip_version_mock.Call(version);
    }

    mock_function::MockFunction<std::monostate, uint8_t, uint16_t, TokenPtr> ip_pkt_length_mock;
    std::monostate ip_pkt_length(uint8_t version, uint16_t length, TokenPtr comparator) override {
        return ip_pkt_length_mock.Call(version, length, comparator);
    }

    mock_function::MockFunction<std::monostate, uint8_t, uint8_t> ip_protocol_mock;
    std::monostate ip_protocol(uint8_t version, uint8_t protocol) override {
        return ip_protocol_mock.Call(version, protocol);
    }

    mock_function::MockFunction<std::monostate, uint32_t, TokenPtr> ipv4_address_mock;
    std::monostate ipv4_address(uint32_t address, TokenPtr type) override {
        return ipv4_address_mock.Call(address, type);
    }

    mock_function::MockFunction<std::monostate, std::array<uint8_t, IP6_ADDR_LEN>, TokenPtr>
        ipv6_address_mock;
    std::monostate ipv6_address(std::array<uint8_t, IP6_ADDR_LEN> address, TokenPtr addr_type)
        override {
        return ipv6_address_mock.Call(address, addr_type);
    }

    mock_function::MockFunction<std::monostate, std::vector<std::pair<uint16_t, uint16_t>>,
                                TokenPtr>
        ports_mock;
    std::monostate ports(std::vector<std::pair<uint16_t, uint16_t>> ranges, TokenPtr port_type)
        override {
        return ports_mock.Call(ranges, port_type);
    }

    mock_function::MockFunction<std::monostate, std::monostate> negation_mock;
    std::monostate negation(std::monostate filter) override {
        return negation_mock.Call(filter);
    }

    mock_function::MockFunction<std::monostate, std::monostate, std::monostate> conjunction_mock;
    std::monostate conjunction(std::monostate left, std::monostate right) override {
        return conjunction_mock.Call(left, right);
    }

    mock_function::MockFunction<std::monostate, std::monostate, std::monostate> disjunction_mock;
    std::monostate disjunction(std::monostate left, std::monostate right) override {
        return disjunction_mock.Call(left, right);
    }

    explicit MockFilterBuilder(const Tokenizer& tokenizer)
        : FilterBuilder<std::monostate>(tokenizer) {}

#define NETDUMP_APPLY_TO_ALL_MOCKS(fn) \
    do {                               \
        frame_length_mock.fn();        \
        ethertype_mock.fn();           \
        mac_mock.fn();                 \
        ip_version_mock.fn();          \
        ip_pkt_length_mock.fn();       \
        ip_protocol_mock.fn();         \
        ipv4_address_mock.fn();        \
        ipv6_address_mock.fn();        \
        ports_mock.fn();               \
        negation_mock.fn();            \
        conjunction_mock.fn();         \
        disjunction_mock.fn();         \
    } while (false)

    inline void verify_and_clear_all() {
        NETDUMP_APPLY_TO_ALL_MOCKS(VerifyAndClear);
    }

    inline void expect_no_call_all() {
        NETDUMP_APPLY_TO_ALL_MOCKS(ExpectNoCall);
    }
#undef NETDUMP_APPLY_TO_ALL_MOCKS
};

// Test token cursor transitions in the parse environment.
TEST(NetdumpParserTests, EnvironmentPlusPlusTest) {
    Tokenizer tkz{};
    Environment env(std::vector<TokenPtr>{tkz.PORT, tkz.HOST});

    EXPECT_EQ(tkz.PORT, *env);
    ++env;
    EXPECT_EQ(tkz.HOST, *env);
}

TEST(NetdumpParserTests, EnvironmentMinusMinusTest) {
    Tokenizer tkz{};
    Environment env(std::vector<TokenPtr>{tkz.TCP, tkz.IP6});

    EXPECT_EQ(tkz.TCP, *env);
    ++env;
    EXPECT_EQ(tkz.IP6, *env);
    --env;
    EXPECT_EQ(tkz.TCP, *env);
}

TEST(NetdumpParserTests, EnvironmentGuardsTest) {
    Tokenizer tkz{};
    Environment env(std::vector<TokenPtr>{tkz.ICMP, tkz.ARP});

    EXPECT_EQ(tkz.ICMP, *env);
    --env;
    EXPECT_EQ(tkz.ICMP, *env);
    ++env;
    ++env;
    EXPECT_TRUE(env.at_end());
    ++env;
    EXPECT_TRUE(env.at_end());
    --env;
    EXPECT_EQ(tkz.ARP, *env);
}

TEST(NetdumpParserTests, EnvironmentEndDereferenceTest) {
    Tokenizer tkz{};
    Environment env(std::vector<TokenPtr>{tkz.ICMP});
    ++env;

    EXPECT_TRUE(env.at_end());
    ASSERT_DEATH([&env]() { *env; });
}

TEST(NetdumpParserTests, EnvironmentFullWalkTest) {
    Tokenizer tkz{};
    Environment env(std::vector<TokenPtr>{tkz.AND, tkz.DNS, tkz.DHCP, tkz.SRC});

    EXPECT_EQ(env.begin(), env.cur());
    EXPECT_EQ(tkz.AND, *env);
    ++env;
    EXPECT_EQ(tkz.DNS, *env);
    --env;
    EXPECT_EQ(tkz.AND, *env);
    --env;
    EXPECT_EQ(tkz.AND, *env);
    ++env;
    ++env;
    EXPECT_EQ(tkz.DHCP, *env);
    ++env;
    EXPECT_FALSE(env.at_end());
    EXPECT_EQ(tkz.SRC, *env);
    ++env;
    EXPECT_EQ(env.end(), env.cur());
    ++env;
    EXPECT_TRUE(env.at_end());
    --env;
    EXPECT_EQ(tkz.SRC, *env);

    EXPECT_FALSE(env.has_error());
    env.error_loc = env.cur();
    env.error_cause = "cause";

    env.reset();
    EXPECT_EQ(tkz.AND, *env);

    EXPECT_TRUE(env.has_error());
    env.clear_error();
    EXPECT_EQ(std::nullopt, env.error_loc);
    EXPECT_EQ("", env.error_cause);
}

// Totally invalid filter string should end up with no filter operation calls after parsing.
TEST(NetdumpParserTests, ParseErrorTest) {
    Tokenizer tkz{};
    MockFilterBuilder builder(tkz);
    Parser parser(tkz);
    builder.expect_no_call_all();
    std::variant<std::monostate, ParseError> filter = parser.parse("mumble jumble", &builder);
    // Error state as expected if `filter` holds a `ParseError`.
    EXPECT_TRUE(std::holds_alternative<ParseError>(filter));
    builder.verify_and_clear_all();
}

class TestParser : public Parser {
public:
    void highlight_error_test() {
        Environment env(std::vector<TokenPtr>{tkz_.AND, tkz_.DNS, tkz_.DHCP});

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
        expect_string2 << ANSI_HIGHLIGHT_ERROR << tkz_.AND->get_term() << ANSI_RESET << " "
                       << tkz_.DNS->get_term() << " "
                       << tkz_.DHCP->get_term();
        EXPECT_STR_EQ(expect_string2.str().c_str(), highlight_error("spec", &env).c_str());

        std::stringstream expect_string3;
        env.reset();
        ++env;
        env.error_loc = env.cur();
        --env; // This tests error is highlighted by error location, not `env` state.
        expect_string3 << tkz_.AND->get_term() << " "
                       << ANSI_HIGHLIGHT_ERROR << tkz_.DNS->get_term() << ANSI_RESET << " "
                       << tkz_.DHCP->get_term();
        EXPECT_STR_EQ(expect_string3.str().c_str(), highlight_error("spec", &env).c_str());

        std::stringstream expect_string4;
        env.reset();
        ++env;
        ++env;
        env.error_loc = env.cur();
        expect_string4 << tkz_.AND->get_term() << " "
                       << tkz_.DNS->get_term() << " "
                       << ANSI_HIGHLIGHT_ERROR << tkz_.DHCP->get_term() << ANSI_RESET;
        EXPECT_STR_EQ(expect_string4.str().c_str(), highlight_error("spec", &env).c_str());
    }

    explicit TestParser(const Tokenizer& tokenizer)
        : Parser(tokenizer) {}
};

TEST(NetdumpParserTests, HighlightErrorTest) {
    TestParser(Tokenizer{}).highlight_error_test();
}

TEST(NetdumpParserTests, SyntaxDisplayTest) {
    // Manual inspection only.
    // TODO(xianglong): Remove once filter syntax usage display is integrated.
    parser_syntax(&std::cerr);
}

} // namespace netdump::parser::test
