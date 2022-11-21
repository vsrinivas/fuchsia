// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/lib/runtime.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/shell/josh/lib/js_testing_utils.h"
#include "third_party/quickjs/quickjs.h"

namespace fs = std::filesystem;

namespace shell {

class RuntimeTest : public JsTest {
 protected:
  void SetUp() override {
    JsTest::SetUp();

    // Always enable STD libraries
    if (!ctx_->InitStd()) {
      ctx_->DumpError();
      FAIL();
    }

    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread(), ZX_OK);
    ASSERT_EQ(ZX_OK, memfs_install_at(loop_->dispatcher(), "/test_tmp", &fs_));

    // Make sure file creation is OK so memfs is running OK.
    char tmpfs_test_file[] = "/test_tmp/runtime.test.XXXXXX";
    ASSERT_NE(mkstemp(tmpfs_test_file), -1);
  }

  void TearDown() override {
    // Synchronously clean up.
    sync_completion_t unmounted;
    memfs_free_filesystem(fs_, &unmounted);
    sync_completion_wait(&unmounted, zx::duration::infinite().get());
    fs_ = nullptr;

    loop_->Shutdown();

    JsTest::TearDown();
  }

  std::unique_ptr<async::Loop> loop_;
  memfs_filesystem_t *fs_;
};

TEST_F(RuntimeTest, TestNonExistsStartupScriptsDir) {
  // Going to add startup scripts under this path
  std::string startup_path("/test_tmp/js_startup");
  ASSERT_EQ(false, fs::is_directory(startup_path));

  // Load startup scripts from a directory that doesn't exist
  ASSERT_EQ(false, ctx_->InitStartups(startup_path));
}

TEST_F(RuntimeTest, TestEmptyStartupScriptsDir) {
  // Going to add startup scripts under this path
  std::string startup_path("/test_tmp/js_startup");

  // Create a directory for startup scripts
  ASSERT_EQ(true, fs::create_directory(startup_path));
  ASSERT_EQ(true, fs::is_directory(startup_path));

  // Load startup scripts from an empty directory
  // load nothing when sequence.json doesn't exist
  ASSERT_EQ(true, ctx_->InitStartups(startup_path));
}

TEST_F(RuntimeTest, TestStartupScripts) {
  // Create a directory for startup scripts
  std::string startup_path("/test_tmp/js_startup");
  ASSERT_EQ(true, fs::create_directory(startup_path));
  ASSERT_EQ(true, fs::is_directory(startup_path));

  std::ofstream test_script;
#define WRITE_ORDER_FILE_JS(val)                                                             \
  do {                                                                                       \
    test_script << "let file = std.open('/test_tmp/js_startup/orders', 'a+');" << std::endl; \
    test_script << "file.puts('" << val << ",');" << std::endl;                              \
    test_script << "file.close();" << std::endl;                                             \
    test_script << "export { GetValue }" << std::endl;                                       \
    test_script.close();                                                                     \
  } while (0)

// Create name.js which returns "value" when GetValue is called
#define WRITE_MODULE_JS(name, value)                                               \
  do {                                                                             \
    test_script.open(startup_path + "/" name ".js");                               \
    test_script << "function GetValue() { return " << value << "; }" << std::endl; \
    WRITE_ORDER_FILE_JS(value);                                                    \
  } while (0)

  // Add startup scripts into the directory
  WRITE_MODULE_JS("module3", 3);   // module3.js
  WRITE_MODULE_JS("Module4", 4);   // module4.js
  WRITE_MODULE_JS("module5", 5);   // module5.js
  WRITE_MODULE_JS("MODULE2", 2);   // module2.js
  WRITE_MODULE_JS("module1", 1);   // module1.js
  WRITE_MODULE_JS("_module6", 6);  // module6.js

  std::ofstream test_sequence_json;
  test_sequence_json.open(startup_path + "/startup.json");
  test_sequence_json << R"(
    {
      "startup": [
        "module1.js",
        "MODULE2.js",
        "../js_startup/module3.js",
        "Module4.js",
        "_module6.js"
      ]
    }
  )" << std::endl;
  test_sequence_json.close();

  // Load startup scripts
  ASSERT_EQ(true, ctx_->InitStartups(startup_path));

  // Validate the results
  ASSERT_EQ(true, Eval(R"(
        // Make sure modules are loaded correctly
        validations = [
            [module1.GetValue, 1],
            [MODULE2.GetValue, 2],
            [module3.GetValue, 3],
            [Module4.GetValue, 4],
            [_module6.GetValue, 6],
        ];
        for ([func, value] of validations) {
            if (func() != value) {
                throw `Module loaded incorrectly! Expecting ${value}, got ${func()}`;
            }
        }

        // Expect startup scripts to run in the correct order
        validation_string = "1,2,3,4,6,";
        file = std.open('/test_tmp/js_startup/orders', 'r+');
        read_string = file.readAsString();
        if (read_string != validation_string) {
            throw `Modules loaded in incorrect order! Expecting ${validation_string}, got ${read_string}`;
        }
        file.close();
    )"));
  loop_->RunUntilIdle();
}

}  // namespace shell
