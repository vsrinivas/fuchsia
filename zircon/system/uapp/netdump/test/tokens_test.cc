// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tokens.h"

#include <zxtest/zxtest.h>

namespace netdump::test {

class TokensTester : public Tokenizer {
 public:
  void LiteralTest() {
    TokenPtr token = literal("testestest");
    TokenPtr other = literal("testestest");

    EXPECT_EQ("testestest", token->get_term());
    EXPECT_EQ("testestest", other->get_term());

    EXPECT_FALSE(token == other);
  }

  void KeywordTest() {
    TokenPtr before = literal("KEYWORD");
    TokenPtr token = keyword("KEYWORD", 'k');
    TokenPtr other = keyword("KEYWORD", 'k');
    TokenPtr after = literal("KEYWORD");

    EXPECT_EQ("KEYWORD", token->get_term());
    EXPECT_EQ('k', token->get_tag<char>());

    // Check the behavior expected of a keyword token, and that the `literal` function has not
    // registered anything before or after a `keyword` call.
    EXPECT_TRUE(token == other);    // Keyword token remains the same.
    EXPECT_FALSE(token == before);  // `literal` did not register new keyword.
    EXPECT_TRUE(token == after);    // `literal` did not overwrite keyword registration.

    ASSERT_DEATH([this]() { keyword("KEYWORD", 'w'); });  // Redefining `tag` not allowed.
  }

  void SynonymTest() {
    TokenPtr token = keyword("HELLO", "CIAO");
    TokenPtr keyword_syn = keyword("CIAO");
    TokenPtr literal_syn = literal("CIAO");
    TokenPtr add_syn = keyword("CIAO", "NIHAO");
    TokenPtr syn = keyword("NIHAO");

    EXPECT_EQ("HELLO", token->get_term());
    EXPECT_TRUE(token == keyword_syn);
    EXPECT_TRUE(token == add_syn);
    EXPECT_TRUE(token == syn);
  }

  static void CheckTokenVectorString(std::vector<TokenPtr> expected_tokens,
                                     std::vector<TokenPtr> got_tokens) {
    EXPECT_EQ(expected_tokens.size(), got_tokens.size());
    auto exp = expected_tokens.begin();
    for (const TokenPtr& token : got_tokens) {
      EXPECT_EQ((*exp)->get_term(), token->get_term());
      ++exp;
    }
  }

  void BasicTokenizeTest() {
    TokenPtr kwa = keyword("kwa");
    TokenPtr kwb = keyword("kwb");
    TokenPtr kwc = keyword("kwc", "kwd");
    TokenPtr lita = literal("lita");
    TokenPtr litb = literal("litb");
    std::string input = "kwa kwb\tkwa    kwc\t\tlita\nkwb\t\nkwd\n\nlitb";
    std::vector<TokenPtr> tokens = tokenize(input);
    std::vector<TokenPtr> expected_tokens = {kwa, kwb, kwa, kwc, lita, kwb, kwc, litb};

    std::vector<TokenPtr> empty = tokenize("");
    EXPECT_TRUE(empty.empty());

    CheckTokenVectorString(expected_tokens, tokens);
    // Literal tokens should have unique identities.
    EXPECT_NE(tokens[4], lita);
    EXPECT_NE(tokens[7], litb);
  }

  void TokenizeRealKeywordsTest() {
    std::string input = "( ether src ) and ( ip6 or ip4 ) and ( tcp dst port )";
    std::vector<TokenPtr> tokens = tokenize(input);
    std::vector<TokenPtr> expected_tokens = {L_PARENS, ETHER, SRC,  R_PARENS, AND, L_PARENS,
                                             IP6,      OR,    IP,   R_PARENS, AND, L_PARENS,
                                             TCP,      DST,   PORT, R_PARENS};
    EXPECT_EQ(expected_tokens, tokens);
  }

  void TokenizeKeywordsLiteralsStringTest() {
    TokenPtr xxx = literal("xxx");
    TokenPtr twentythree = literal("23");
    std::string input = "( ether src xxx ) and ( ip6 or ip4 ) and ( tcp dst port 23 )";
    std::vector<TokenPtr> tokens = tokenize(input);
    std::vector<TokenPtr> expected_tokens = {L_PARENS, ETHER, SRC, xxx,  R_PARENS,    AND,
                                             L_PARENS, IP6,   OR,  IP,   R_PARENS,    AND,
                                             L_PARENS, TCP,   DST, PORT, twentythree, R_PARENS};
    CheckTokenVectorString(expected_tokens, tokens);
  }

  void VisitorTest() {
    TokenPtr token = literal("testestest");
    TokenPtr port_token = port("20-30");
    bool token_fail = false;
    bool port_token_fail = false;
    std::string literal_str = "";
    uint16_t beg_port = 0;
    uint16_t end_port = 0;

    auto token_visitor =
        FunctionalTokenVisitor([&literal_str](TokenPtr t) { literal_str = t->get_term(); },
                               [&token_fail](PortTokenPtr /*t*/) { token_fail = true; });

    auto port_token_visitor =
        FunctionalTokenVisitor([&port_token_fail](TokenPtr /*t*/) { port_token_fail = true; },
                               [&beg_port, &end_port](PortTokenPtr t) {
                                 beg_port = t->begin();
                                 end_port = t->end();
                               });

    token->accept(&token_visitor);
    port_token->accept(&port_token_visitor);

    EXPECT_EQ("testestest", literal_str);
    EXPECT_EQ(20, beg_port);
    EXPECT_EQ(30, end_port);
    EXPECT_FALSE(token_fail);
    EXPECT_FALSE(port_token_fail);
  }

  static inline FunctionalTokenVisitor PortTokenVisitor(bool* fail, uint16_t* beg_port,
                                                        uint16_t* end_port) {
    FunctionalTokenVisitor visitor(
        // Sets some failure `bool` to true when the visitor finds a non-port token.
        [fail](TokenPtr /*t*/) { *fail = true; },
        // Sets the port range when the visitor finds a port token.
        [beg_port, end_port](PortTokenPtr t) {
          *beg_port = t->begin();
          *end_port = t->end();
        });
    return visitor;
  }

  void NamedPortTest() {
    TokenPtr token = named_port("FancyPort", "FANCY", 10, 1000, 42);
    TokenPtr syn = named_port("FANCY", "FANCIER", 20, 2000);
    TokenPtr add_syn = named_port("FANCIER", 30, 3000);
    bool fail = false;
    uint16_t beg_port = 0;
    uint16_t end_port = 0;
    FunctionalTokenVisitor visitor = PortTokenVisitor(&fail, &beg_port, &end_port);
    token->accept(&visitor);

    EXPECT_EQ("FancyPort", token->get_term());
    EXPECT_EQ(42, token->get_tag<uint8_t>());
    EXPECT_EQ(10, beg_port);
    EXPECT_EQ(1000, end_port);
    EXPECT_FALSE(fail);
    EXPECT_TRUE(token == syn);
    EXPECT_TRUE(token == add_syn);
  }

  static inline std::string CheckPort(const TokenPtr& token, bool* fail, uint16_t* beg_port,
                                      uint16_t* end_port) {
    *fail = false;
    *beg_port = 0;
    *end_port = 0;
    FunctionalTokenVisitor visitor = PortTokenVisitor(fail, beg_port, end_port);
    token->accept(&visitor);
    return token->get_term();
  }

  using PortRanges = std::vector<std::optional<PortRange>>;
  static void CheckPortVector(std::vector<TokenPtr> tokens, std::vector<std::string> terms,
                              PortRanges ranges) {
    EXPECT_EQ(tokens.size(), terms.size());
    EXPECT_EQ(tokens.size(), ranges.size());
    auto term = terms.begin();
    auto range = ranges.begin();
    bool fail = false;
    uint16_t beg_port = 0;
    uint16_t end_port = 0;
    for (const TokenPtr& token : tokens) {
      CheckPort(token, &fail, &beg_port, &end_port);
      if (*range == std::nullopt) {
        EXPECT_TRUE(fail);
      } else {
        EXPECT_FALSE(fail);
        EXPECT_EQ((*range)->first, beg_port);
        EXPECT_EQ((*range)->second, end_port);
      }
      EXPECT_EQ(*term, token->get_term());
      ++term;
      ++range;
    }
  }

  void PortTest() {
    std::vector<TokenPtr> tokens = {named_port("MYPORT", 1, 1),
                                    port("MYPORT"),
                                    port("42"),
                                    port("25-35"),
                                    port("YOURPORT"),
                                    port("42,51"),
                                    port("-42"),
                                    port("1--42"),
                                    port("100-50"),
                                    port("55-66000"),
                                    port("1-ftpxfer"),
                                    port("ftpxfer-ftpctl")};
    std::vector<std::string> terms = {"MYPORT",   "MYPORT",   "42",        "25-35",
                                      "YOURPORT", "42,51",    "-42",       "1--42",
                                      "100-50",   "55-66000", "1-ftpxfer", "ftpxfer-ftpctl"};
    PortRanges ranges = {std::optional(PortRange(1, 1)),
                         std::optional(PortRange(1, 1)),
                         std::optional(PortRange(42, 42)),
                         std::optional(PortRange(25, 35)),
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt};
    CheckPortVector(tokens, terms, ranges);
  }

  void PortTokenizationTest() {
    TokenPtr named1 = named_port("MYPORT", 1, 1);
    TokenPtr named2 = named_port("THISPORT", "THATPORT", 2, 2);
    std::string input = "6!15!10-20!MYPORT!YOURPORT!30-10!  !!37!THATPORT";
    // In actual use we will probably use ',' as the delimiter.
    // Here we test that '!' works too.
    std::vector<TokenPtr> tokens = mult_ports('!', input);
    std::vector<std::string> terms = {"6",  "15", "10-20", "MYPORT",  "YOURPORT", "30-10",
                                      "  ", "",   "37",    "THISPORT"};  // Careful with synonyms!
    PortRanges ranges = {std::optional(PortRange(6, 6)),
                         std::optional(PortRange(15, 15)),
                         std::optional(PortRange(10, 20)),
                         std::optional(PortRange(1, 1)),
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::optional(PortRange(37, 37)),
                         std::optional(PortRange(2, 2))};

    std::vector<TokenPtr> empty = mult_ports('!', "");
    EXPECT_TRUE(empty.empty());

    CheckPortVector(tokens, terms, ranges);
  }

  void RealNamedPortsTokenizationTest() {
    std::string input = "75,21,10-20,ssh,http,dbglog,ftpxfer,ftpctl,badname,,SSH";
    std::vector<TokenPtr> tokens = mult_ports(',', input);
    std::vector<std::string> terms = {"75",      "21",     "10-20",   "ssh", "http", "dbglog",
                                      "ftpxfer", "ftpctl", "badname", "",    "SSH"};
    PortRanges ranges = {std::optional(PortRange(75, 75)),
                         std::optional(PortRange(21, 21)),
                         std::optional(PortRange(10, 20)),
                         std::optional(PortRange(22, 22)),
                         std::optional(PortRange(80, 80)),
                         std::optional(PortRange(DEBUGLOG_PORT, DEBUGLOG_PORT)),
                         std::optional(PortRange(20, 20)),
                         std::optional(PortRange(21, 21)),
                         std::nullopt,
                         std::nullopt,
                         std::nullopt};

    std::vector<TokenPtr> empty = mult_ports(',', "");
    EXPECT_TRUE(empty.empty());

    CheckPortVector(tokens, terms, ranges);
  }

  void OneOfTest() {
    TokenPtr t1 = keyword("KEYWORD");
    TokenPtr t2 = literal("KEYWORD");
    TokenPtr t3 = literal("foo");
    TokenPtr t4 = literal("foo");
    TokenPtr t5 = literal("bar");
    TokenPtr t6 = port("50");
    TokenPtr t7 = named_port("SOMEPORT", 50, 50);
    TokenPtr t8 = port("SOMEPORT");

    EXPECT_TRUE(t1->one_of(t3, t4, t5, t6, t1));
    EXPECT_TRUE(t2->one_of(t3, t4, t5, t6, t1));
    EXPECT_FALSE(t3->one_of(t4, t5, t6, t1));
    EXPECT_TRUE(t4->one_of(t1, t2, t4, t5, t6));
    EXPECT_TRUE(t5->one_of(t5));
    EXPECT_FALSE(t6->one_of(port("50"), t7, t8));
    EXPECT_TRUE(t7->one_of(t8));
    EXPECT_TRUE(t8->one_of(t1, t5, t7));
  }
};

}  // namespace netdump::test

#define NETDUMP_TEST(fn) \
  TEST(NetdumpTokensTest, fn) { netdump::test::TokensTester().fn(); }
NETDUMP_TEST(LiteralTest)
NETDUMP_TEST(KeywordTest)
NETDUMP_TEST(SynonymTest)
NETDUMP_TEST(BasicTokenizeTest)
NETDUMP_TEST(TokenizeRealKeywordsTest)
NETDUMP_TEST(TokenizeKeywordsLiteralsStringTest)
NETDUMP_TEST(VisitorTest)
NETDUMP_TEST(NamedPortTest)
NETDUMP_TEST(PortTest)
NETDUMP_TEST(PortTokenizationTest)
NETDUMP_TEST(RealNamedPortsTokenizationTest)
NETDUMP_TEST(OneOfTest)
#undef NETDUMP_TEST
