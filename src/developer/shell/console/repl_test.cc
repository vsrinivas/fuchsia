// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "repl.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <array>
#include <fstream>
#include <queue>
#include <string>

#include "gtest/gtest.h"
#include "li.h"
#include "src/developer/shell/lib/runtime.h"
#include "src/lib/line_input/line_input.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

extern "C" const uint8_t qjsc_repl_init[];
extern "C" const uint32_t qjsc_repl_init_size;

namespace shell::repl {

class TestRepl : public Repl {
 public:
  TestRepl(JSContext* ctx, const std::string& prompt, bool ev, bool pr)
      : Repl(ctx, prompt, [this](const std::string& s) { HandleLine(s); }), eval_(ev), print_(pr) {}

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

  void Write(const char* output) {
    if (print_)
      Repl::Write(output);
    else
      output_.push(output);
  }

 protected:
  void HandleLine(const std::string& line) {
    full_lines_.push(line);
    Repl::HandleLine(line);
  }

  void EvalCmd(std::string& cmd) {
    full_cmds_.push(cmd);
    if (eval_) {
      Repl::EvalCmd(cmd);
    } else {
      ShowPrompt();  // no evaluation, which makes this call
    }
  }

 private:
  std::queue<std::string> full_lines_;
  std::queue<std::string> full_cmds_;
  bool eval_ = false;
  bool print_ = false;
  std::queue<const char*> output_;
};

TEST(Repl, Sanity) {
  // set up a file to output to
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  ASSERT_EQ(ZX_OK, memfs_install_at(loop.dispatcher(), "/test_tmp2"));
  constexpr const char* name = "/test_tmp2/tmp.XXXXXX";
  std::unique_ptr<char[]> buffer(new char[strlen(name) + 1]);
  strcpy(buffer.get(), name);
  int cfd = mkstemp(buffer.get());
  ASSERT_NE(cfd, -1);
  std::string filename = buffer.get();
  FILE* output_fd = fdopen(cfd, "rw+");

  // set up the repl
  shell::Runtime rt;
  ASSERT_NE(rt.Get(), nullptr);
  shell::Context ctx(&rt);
  ASSERT_NE(ctx.Get(), nullptr);
  ASSERT_NE(0, ctx.InitStd());
  ASSERT_NE(0, ctx.InitBuiltins("/pkg/data/fidling", "/pkg/data/lib"));
  JSContext* ctx_ptr = ctx.Get();
  js_std_add_helpers(ctx_ptr, 0, nullptr);
  ASSERT_NE(shell::li::LiModuleInit(ctx_ptr, "li_internal"), nullptr);
  js_std_eval_binary(ctx_ptr, qjsc_repl_init, qjsc_repl_init_size, 0);

  // uninstall repl read handler and get a pointer to the repl
  std::string script = "os.setReadHandler(std.in.fileno(), null); repl.repl;";
  JSValue res =
      JS_Eval(ctx_ptr, script.c_str(), script.length(), "<evalScript>", JS_EVAL_TYPE_GLOBAL);
  repl::Repl* repl = li::GetRepl(ctx_ptr, res);

  repl->ChangeOutput(output_fd);

  std::string test_string = "print(4);\n";
  repl->FeedInput(reinterpret_cast<unsigned char*>(test_string.data()), test_string.size());

  fflush(output_fd);
  std::ifstream in(filename.c_str());
  std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string expected("undefined\n");
  ASSERT_STREQ(expected.c_str(), actual.c_str());

  std::string test_string2 =
      "ccc = new Promise(function(resolve, reject){os.setTimeout(() => resolve('d'), 1);});\n";
  repl->FeedInput(reinterpret_cast<unsigned char*>(test_string2.data()), test_string2.size());
  js_std_loop(ctx_ptr);

  fflush(output_fd);
  std::ifstream in2(filename.c_str());
  std::string actual2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
  std::string expected2("undefined\nd\n");
  ASSERT_STREQ(expected2.c_str(), actual2.c_str());
}

TEST(Repl, SpecialCharacters) {
  JSContext* ctx_ptr = 0;
  TestRepl repl(ctx_ptr, "li >", false, false);

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
  TestRepl repl(ctx_ptr, "li >", false, false);

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

TEST(Repl, TabCompletionAndHistory) {
  // set up a file to output to
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  ASSERT_EQ(ZX_OK, memfs_install_at(loop.dispatcher(), "/test_tmpTabCompletion"));
  constexpr const char* name = "/test_tmpTabCompletion/tmp.XXXXXX";
  std::unique_ptr<char[]> buffer(new char[strlen(name) + 1]);
  strcpy(buffer.get(), name);
  int cfd = mkstemp(buffer.get());
  ASSERT_NE(cfd, -1);
  std::string filename = buffer.get();
  FILE* output_fd = fdopen(cfd, "rw+");

  // set up the repl
  shell::Runtime rt;
  ASSERT_NE(rt.Get(), nullptr);
  shell::Context ctx(&rt);
  ASSERT_NE(ctx.Get(), nullptr);
  ASSERT_NE(0, ctx.InitStd());
  ASSERT_NE(0, ctx.InitBuiltins("/pkg/data/fidling", "/pkg/data/lib"));
  JSContext* ctx_ptr = ctx.Get();
  js_std_add_helpers(ctx_ptr, 0, nullptr);
  ASSERT_NE(shell::li::LiModuleInit(ctx_ptr, "li_internal"), nullptr);
  js_std_eval_binary(ctx_ptr, qjsc_repl_init, qjsc_repl_init_size, 0);

  // uninstall repl read handler and get a pointer to the repl
  std::string script = "os.setReadHandler(std.in.fileno(), null); repl.repl;";
  JSValue res =
      JS_Eval(ctx_ptr, script.c_str(), script.length(), "<evalScript>", JS_EVAL_TYPE_GLOBAL);
  repl::Repl* repl = reinterpret_cast<repl::Repl*>(li::GetRepl(ctx_ptr, res));

  repl->ChangeOutput(output_fd);

  std::string test_string = "a = \"dddd\";\n";
  repl->FeedInput(reinterpret_cast<unsigned char*>(test_string.data()), test_string.size());
  std::string test_string2 = "b =a.le\t;\n";
  repl->FeedInput(reinterpret_cast<unsigned char*>(test_string2.data()), test_string2.size());

  fflush(output_fd);
  std::ifstream in(filename.c_str());
  std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string expected("dddd\n4\n");
  ASSERT_STREQ(expected.c_str(), actual.c_str());

  // testing history, repeats the previous command
  std::string test_string3 = "\x1b[A\n";
  repl->FeedInput(reinterpret_cast<unsigned char*>(test_string3.data()), test_string3.size());

  fflush(output_fd);
  std::ifstream in2(filename.c_str());
  std::string actual2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
  std::string expected2("dddd\n4\n4\n");
  ASSERT_STREQ(expected2.c_str(), actual2.c_str());
}

}  // namespace shell::repl
