// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/spawn.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <array>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <task-utils/walker.h>

#include "src/developer/shell/josh/lib/js_testing_utils.h"
#include "zx.h"

namespace shell {

class TaskTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

TEST_F(TaskTest, SimplePs) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  // Loop up-front to populate the svc object, which is done via a promise.
  js_std_loop(ctx_->Get());
  std::string test_string = R"(
      globalThis.resultOne = undefined;
      task.ps().
        then((result) => {
            globalThis.resultOne = result; }).
        catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultOne = e;});
  )";
  ASSERT_TRUE(Eval(test_string));
  js_std_loop(ctx_->Get());
  test_string = R"(
      let res = globalThis.resultOne;
      if (res instanceof Error) {
        throw res;
      }
      if (res.size <= 0) {
        throw "No tasks found by ps?";
      }
      res.forEach((value, key, map) => {
          if (!key.hasOwnProperty("name") || !key.hasOwnProperty("info")) {
              throw "Missing task information in " + JSON.stringify(key);
          }
       });
  )";
  ASSERT_TRUE(Eval(test_string));
}

TEST_F(TaskTest, Kill) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  // get root job, to create a subprocess of it, that will be dicoverable in the job tree
  js_std_loop(ctx_->Get());
  std::string test_string0 = R"(
      fidl.loadLibrary('fuchsia.kernel');
      let promiseRootJobResult = svc.fuchsia_kernel_RootJob.Get();
      promiseRootJobResult.
              then((result) => {
                  globalThis.resultOne = result; })
  )";
  ASSERT_TRUE(Eval(test_string0));
  js_std_loop(ctx_->Get());
  std::string test_string1 = R"(
      globalThis.resultOne['job']._handle;
  )";
  JSValue handle_to_root_js =
      JS_Eval(ctx_->Get(), test_string1.c_str(), test_string1.length(), "<evalScript>", 0);
  if (JS_IsException(handle_to_root_js)) {
    ctx_->DumpError();
  }
  ASSERT_FALSE(JS_IsException(handle_to_root_js));
  zx_handle_info_t handle_to_root = zx::HandleFromJsval(handle_to_root_js);

  // spawn a proccess, child of root job and get its koid
  const char* argv[] = {"/pkg/bin/spawn_child_test_util", nullptr};
  std::vector<fdio_spawn_action_t> actions;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t process_handle;
  zx_status_t status2 = fdio_spawn_etc(handle_to_root.handle, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                       nullptr,  // Environ
                                       actions.size(), actions.data(), &process_handle, err_msg);
  ASSERT_EQ(status2, ZX_OK) << "Failed to spawn command (" << status2 << "): " << err_msg;
  zx_info_handle_basic_t info;
  ASSERT_EQ(
      zx_object_get_info(process_handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL),
      ZX_OK);
  loop.RunUntilIdle();

  // kill the process
  std::string test_string2 =
      "globalThis.resultTwo = undefined;"
      "task.kill(" +
      std::to_string(info.koid) +
      ").then((result) => {"
      "globalThis.resultTwo = result; })."
      "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
  ASSERT_TRUE(Eval(test_string2));
  // task.kill() is async, the loop is needed to ensure it is executed
  js_std_loop(ctx_->Get());
  std::string test_string3 = R"(
      let res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
  ASSERT_TRUE(Eval(test_string3));
}

TEST_F(TaskTest, KillAll) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  // get root job, to create a subprocess of it, that will be dicoverable in the job tree
  js_std_loop(ctx_->Get());
  std::string test_string0 = R"(
      fidl.loadLibrary('fuchsia.kernel');
      let promiseRootJobResult = svc.fuchsia_kernel_RootJob.Get();
      promiseRootJobResult.
              then((result) => {
                  globalThis.resultOne = result; })
  )";
  ASSERT_TRUE(Eval(test_string0));
  js_std_loop(ctx_->Get());
  std::string test_string1 = R"(
      globalThis.resultOne['job']._handle;
  )";
  JSValue handle_to_root_js =
      JS_Eval(ctx_->Get(), test_string1.c_str(), test_string1.length(), "<evalScript>", 0);
  if (JS_IsException(handle_to_root_js)) {
    ctx_->DumpError();
  }
  ASSERT_FALSE(JS_IsException(handle_to_root_js));
  zx_handle_info_t handle_to_root = zx::HandleFromJsval(handle_to_root_js);

  // get koid of current process to make the spawned name unique
  zx_handle_t cur_process_handle = zx_process_self();
  zx_info_handle_basic_t info_cur_process;
  ASSERT_EQ(zx_object_get_info(cur_process_handle, ZX_INFO_HANDLE_BASIC, &info_cur_process,
                               sizeof(info_cur_process), NULL, NULL),
            ZX_OK);

  // spawn a proccess, child of root job, named spawnChild+cur_process_koid
  const char* argv[] = {"/pkg/bin/spawn_child_test_util", nullptr};
  std::string process_name = "spawnChild" + std::to_string(info_cur_process.koid);
  std::vector<fdio_spawn_action_t> actions;
  actions.push_back({.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = process_name.c_str()}});
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t process_handle;
  zx_status_t status2 = fdio_spawn_etc(handle_to_root.handle, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                       nullptr,  // Environ
                                       actions.size(), actions.data(), &process_handle, err_msg);
  ASSERT_EQ(status2, ZX_OK) << "Failed to spawn command (" << status2 << "): " << err_msg;
  loop.RunUntilIdle();

  // kill the process by name
  std::string test_string2 =
      "globalThis.resultTwo = undefined;"
      "task.killall(\"" +
      process_name +
      "\").then((result) => {"
      "globalThis.resultTwo = result; })."
      "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
  ASSERT_TRUE(Eval(test_string2));
  // task.kill() is async, the loop is needed to ensure it is executed
  js_std_loop(ctx_->Get());
  std::string test_string3 = R"(
      let res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
  ASSERT_TRUE(Eval(test_string3));

  // launch the same process again, to kill it using a regex
  status2 = fdio_spawn_etc(handle_to_root.handle, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                           nullptr,  // Environ
                           actions.size(), actions.data(), &process_handle, err_msg);
  ASSERT_EQ(status2, ZX_OK) << "Failed to spawn command (" << status2 << "): " << err_msg;
  loop.RunUntilIdle();

  // kill the process by regex
  std::string test_string4 =
      "globalThis.resultTwo = undefined;"
      "task.killall(\"[a-z]" +
      process_name.substr(1) +
      "\", \"r\").then((result) => {"
      "globalThis.resultTwo = result; })."
      "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
  ASSERT_TRUE(Eval(test_string4));
  // task.kill() is async, the loop is needed to ensure it is executed
  js_std_loop(ctx_->Get());
  std::string test_string5 = R"(
      res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
  ASSERT_TRUE(Eval(test_string5));
}

}  // namespace shell
