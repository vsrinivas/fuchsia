// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>

#include <gtest/gtest.h>

#include "js_testing_utils.h"
#include "zx.h"

namespace {

class TaskTest : public shell::JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

TEST_F(TaskTest, SimplePs) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  // Loop up-front to populate the svc object, which is done via a promise.
  js_std_loop(ctx_->Get());

  {
    constexpr std::string_view test_string = R"(
      globalThis.resultOne = undefined;
      task.ps().
        then((result) => {
            globalThis.resultOne = result; }).
        catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultOne = e;});
  )";
    ASSERT_TRUE(Eval(test_string));
  }
  js_std_loop(ctx_->Get());

  {
    constexpr std::string_view test_string = R"(
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
}

TEST_F(TaskTest, Kill) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  js_std_loop(ctx_->Get());

  // Get root job, to create a subprocess of it, that will be dicoverable in the
  // job tree.
  {
    constexpr std::string_view test_string = R"(
      fidl.loadLibrary('fuchsia.kernel');
      let promiseRootJobResult = svc.fuchsia_kernel_RootJob.Get();
      promiseRootJobResult.
              then((result) => {
                  globalThis.resultOne = result; })
  )";
    ASSERT_TRUE(Eval(test_string));
  }
  js_std_loop(ctx_->Get());

  zx::handle handle_to_root;
  {
    constexpr std::string_view test_string = R"(
      globalThis.resultOne['job']._handle;
  )";
    JSValue handle_to_root_js =
        JS_Eval(ctx_->Get(), test_string.data(), test_string.size(), "<evalScript>", 0);
    if (JS_IsException(handle_to_root_js)) {
      ctx_->DumpError();
    }
    ASSERT_FALSE(JS_IsException(handle_to_root_js));
    handle_to_root.reset(shell::zx::HandleFromJsval(handle_to_root_js).handle);
  }

  // Spawn a process, child of root job and get its koid.
  const char* argv[] = {"/pkg/bin/spawn_child_test_util", nullptr};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process process_handle;
  {
    zx_status_t status =
        fdio_spawn_etc(handle_to_root.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                       nullptr /* environ */, 0 /* action_count */, nullptr /* actions */,
                       process_handle.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status) << ": " << err_msg;
  }

  zx_info_handle_basic_t info;
  {
    zx_status_t status =
        process_handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  }
  loop.RunUntilIdle();

  // Kill the process.
  {
    const std::string test_string =
        "globalThis.resultTwo = undefined;"
        "task.kill(" +
        std::to_string(info.koid) +
        ").then((result) => {"
        "globalThis.resultTwo = result; })."
        "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
    ASSERT_TRUE(Eval(test_string));
  }

  // task.kill() is async, the loop is needed to ensure it is executed.
  js_std_loop(ctx_->Get());

  {
    constexpr std::string_view test_string = R"(
      let res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
    ASSERT_TRUE(Eval(test_string));
  }
}

TEST_F(TaskTest, KillAll) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

  js_std_loop(ctx_->Get());

  // Get root job, to create a subprocess of it, that will be dicoverable in the
  // job tree.
  {
    constexpr std::string_view test_string = R"(
      fidl.loadLibrary('fuchsia.kernel');
      let promiseRootJobResult = svc.fuchsia_kernel_RootJob.Get();
      promiseRootJobResult.
              then((result) => {
                  globalThis.resultOne = result; })
  )";
    ASSERT_TRUE(Eval(test_string));
  }
  js_std_loop(ctx_->Get());

  zx::handle handle_to_root;
  {
    constexpr std::string_view test_string = R"(
      globalThis.resultOne['job']._handle;
  )";
    JSValue handle_to_root_js =
        JS_Eval(ctx_->Get(), test_string.data(), test_string.size(), "<evalScript>", 0);
    if (JS_IsException(handle_to_root_js)) {
      ctx_->DumpError();
    }
    ASSERT_FALSE(JS_IsException(handle_to_root_js));
    handle_to_root.reset(shell::zx::HandleFromJsval(handle_to_root_js).handle);
  }

  // Get koid of current process to make the spawned name unique.
  zx_info_handle_basic_t self_info;
  {
    zx_status_t status = zx::process::self()->get_info(ZX_INFO_HANDLE_BASIC, &self_info,
                                                       sizeof(self_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  }

  // Spawn a process, child of root job, named spawnChild+koid(self).
  const char* argv[] = {"/pkg/bin/spawn_child_test_util", nullptr};
  const std::string process_name = "spawnChild" + std::to_string(self_info.koid);
  fdio_spawn_action_t actions[] = {{
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name =
          {
              .data = process_name.c_str(),
          },
  }};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process process_handle;
  {
    zx_status_t status = fdio_spawn_etc(handle_to_root.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                        nullptr /* environ */, std::size(actions), actions,
                                        process_handle.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status) << ": " << err_msg;
  }
  loop.RunUntilIdle();

  // Kill the process by name.
  {
    const std::string test_string =
        "globalThis.resultTwo = undefined;"
        "task.killall(\"" +
        process_name +
        "\").then((result) => {"
        "globalThis.resultTwo = result; })."
        "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
    ASSERT_TRUE(Eval(test_string));
  }

  // task.kill() is async, the loop is needed to ensure it is executed.
  js_std_loop(ctx_->Get());

  {
    constexpr std::string_view test_string = R"(
      let res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
    ASSERT_TRUE(Eval(test_string));
  }

  // Launch the same process again, to kill it using a regex.
  {
    zx_status_t status = fdio_spawn_etc(handle_to_root.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                        nullptr /* environ */, std::size(actions), actions,
                                        process_handle.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status) << ": " << err_msg;
  }
  loop.RunUntilIdle();

  // Kill the process by regex.
  {
    const std::string test_string =
        "globalThis.resultTwo = undefined;"
        "task.killall(\"[a-z]" +
        process_name.substr(1) +
        "\", \"r\").then((result) => {"
        "globalThis.resultTwo = result; })."
        "catch((e) => { std.printf(e); std.printf(e.stack); globalThis.resultTwo = e;});";
    ASSERT_TRUE(Eval(test_string));
  }

  // task.kill() is async, the loop is needed to ensure it is executed.
  js_std_loop(ctx_->Get());

  {
    constexpr std::string_view test_string = R"(
      res = globalThis.resultTwo;
      if (res instanceof Error) {
        throw res;
      }
      if (res != undefined) {
        throw res;
      }
  )";
    ASSERT_TRUE(Eval(test_string));
  }
}

}  // namespace
