// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "repl.h"

#include <array>
#include <queue>
#include <string>

#include "gtest/gtest.h"
#include "li.h"
#include "runtime.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::repl {

class TestRepl : public Repl {
 public:
  TestRepl(JSContext* ctx, const std::string& prompt, bool ev) : Repl(ctx, prompt), eval(ev) {}

  std::queue<std::string> GetListFullLines() {
    std::queue<std::string> res(full_lines_);
    std::queue<std::string>().swap(full_lines_);
    return res;
  }

  std::queue<std::string> GetListFullCmds() {
    std::queue res(full_cmds_);
    std::queue<std::string>().swap(full_cmds_);
    return res;
  }

  std::queue<const char*> GetListOutputs() {
    std::queue res(output_);
    std::queue<const char*>().swap(output_);
    return res;
  }

  std::string PublicOpenSymbols(std::string& cmd) { return OpenSymbols(cmd); }

 protected:
  void HandleLine(const std::string& line) {
    full_lines_.push(line);
    Repl::HandleLine(line);
  }

  const char* EvalCmd(std::string& cmd) {
    full_cmds_.push(cmd);
    if (eval) {
      output_.push(Repl::EvalCmd(cmd));
      return output_.back();
    }
    return nullptr;  // no JS evaluation here
  }

 private:
  std::queue<std::string> full_lines_;
  std::queue<std::string> full_cmds_;
  bool eval = false;
  std::queue<const char*> output_;
};

TEST(Repl, Sanity) {
  shell::Runtime rt;
  ASSERT_NE(rt.Get(), nullptr);
  shell::Context ctx(&rt);
  ASSERT_NE(ctx.Get(), nullptr);
  ASSERT_NE(0, ctx.InitStd());
  ASSERT_NE(0, ctx.InitBuiltins("/pkg/data/fidling", "/pkg/data/lib"));
  JSContext* ctx_ptr = ctx.Get();
  js_std_add_helpers(ctx_ptr, 0, nullptr);
  ASSERT_NE(shell::li::LiModuleInit(ctx_ptr, "li_internal"), nullptr);
  TestRepl repl(ctx_ptr, "li >", true);

  std::string test_string = "print(3)\n";
  repl.FeedInput(reinterpret_cast<unsigned char*>(test_string.data()), test_string.size());
  std::queue<std::string> res_lines = repl.GetListFullLines();
  std::queue<std::string> res_cmds = repl.GetListFullCmds();
  EXPECT_EQ(res_lines.size(), 1);
  EXPECT_EQ(res_cmds.size(), 1);
  EXPECT_STREQ(res_lines.front().c_str(), test_string.substr(0, test_string.size() - 1).c_str());
  EXPECT_STREQ(res_cmds.front().c_str(), test_string.substr(0, test_string.size() - 1).c_str());

  std::queue<const char*> resOutput = repl.GetListOutputs();
  EXPECT_EQ(resOutput.size(), 1);
  const char* expected = "undefined";
  EXPECT_STREQ(resOutput.front(), expected);
}

TEST(Repl, SpecialCharacters) {
  JSContext* ctx_ptr = 0;
  TestRepl repl(ctx_ptr, "li >", false);

  std::string test_string = "pr\x1b[Dint(3)\n";
  std::string expected = "pint(3)r";
  repl.FeedInput(reinterpret_cast<unsigned char*>(test_string.data()), test_string.size());
  std::queue<std::string> res_lines = repl.GetListFullLines();
  std::queue<std::string> res_cmds = repl.GetListFullCmds();
  EXPECT_EQ(res_lines.size(), 1);
  EXPECT_EQ(res_cmds.size(), 1);
  EXPECT_STREQ(res_lines.front().c_str(), expected.c_str());
  EXPECT_STREQ(res_cmds.front().c_str(), expected.c_str());
}

TEST(Repl, MultipleLines) {
  JSContext* ctx_ptr = 0;
  TestRepl repl(ctx_ptr, "li >", false);

  constexpr int kNumLines = 4;
  std::array<std::string, kNumLines> test_string1 = {"function (\n", "a){\n", "\tprint(a)\n",
                                                     "};\n"};
  std::string cur_cmd = "";
  std::string expected = "function (a){print(a)};";
  std::array<std::string, kNumLines> expected_open_symbols = {"(", "{", "{", ""};
  for (int i = 0; i < kNumLines; i++) {
    repl.FeedInput(reinterpret_cast<unsigned char*>(test_string1[i].data()),
                   test_string1[i].size());
    std::queue<std::string> res_lines = repl.GetListFullLines();
    ASSERT_EQ(res_lines.size(), 1);
    cur_cmd = cur_cmd + res_lines.front();
    EXPECT_STREQ(repl.PublicOpenSymbols(cur_cmd).c_str(), expected_open_symbols[i].c_str());
  }
  std::queue<std::string> res_cmds = repl.GetListFullCmds();
  EXPECT_EQ(res_cmds.size(), 1);
  EXPECT_STREQ(res_cmds.front().c_str(), expected.c_str());

  constexpr int kNumLines2 = 5;
  std::array<std::string, kNumLines2> test_string2 = {"regex = /\n", "[abc\n", "/\n", "]\n",
                                                      "/;\n"};
  std::array<std::string, kNumLines2> expected_lines = {"regex = /", "[abc", "/", "]", "/;"};
  std::string expected2 = "regex = /[abc/]/;";
  std::array<std::string, kNumLines2> expected_open_symbols2 = {"/", "/[", "/[", "/", ""};
  cur_cmd = "";
  for (int i = 0; i < kNumLines2; i++) {
    repl.FeedInput(reinterpret_cast<unsigned char*>(test_string2[i].data()),
                   test_string2[i].size());
    std::queue<std::string> res_lines = repl.GetListFullLines();
    ASSERT_EQ(res_lines.size(), 1);
    cur_cmd = cur_cmd + res_lines.front();
    EXPECT_STREQ(repl.PublicOpenSymbols(cur_cmd).c_str(), expected_open_symbols2[i].c_str());
  }
  res_cmds = repl.GetListFullCmds();
  EXPECT_EQ(res_cmds.size(), 1);
  EXPECT_STREQ(res_cmds.front().c_str(), expected2.c_str());
}
}  // namespace shell::repl
