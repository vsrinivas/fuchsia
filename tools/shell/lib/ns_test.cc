// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <stdlib.h>

#include <array>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "tools/shell/console/console.h"
#include "tools/shell/lib/js_testing_utils.h"

namespace shell {

class NsTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

TEST_F(NsTest, Utf8Decode) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");
  ctx_->Export("util", "/pkg/data/lib");
  // One byte, two bytes, three bytes, four bytes.
  std::string test_string = R"(
    const buffer = new ArrayBuffer(10);
    const view = new DataView(buffer);
    const arr = [0x61, 0xc4, 0x80, 0xef, 0xbc, 0xa1, 0xf0, 0x90, 0x80, 0x80];
    for (let i = 0; i < arr.length; i++) {
      view.setUint8(i, arr[i]);
    }
    const strResult = util.decodeUtf8(view)
    const expectedCodePoints = [97,256,65313,65536];
    // There is one more code unit than code point.
    if (expectedCodePoints.length != strResult.length - 1) {
      throw "String decoding incorrect, expected " + (strResult.length - 1)
        + " chars, got " + expectedCodePoints.length + " (" + strResult + ").";
    }
    // This works because the multi-code-unit char is the last char.
    for (let i = 0; i < expectedCodePoints.length; i++) {
      if (strResult.codePointAt(i) != expectedCodePoints[i]) {
        throw "String decoding incorrect, expected " + strResult.codePointAt(i)
          + " at char " + i + ", got " + codePointResult[i] + "(" + strResult + ").";
      }
    }
  )";
  ASSERT_TRUE(Eval(test_string));
}

// Sanity check test to make sure Hello World works.
TEST_F(NsTest, ListFiles) {
  InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  ASSERT_EQ(ZX_OK, memfs_install_at(loop.dispatcher(), "/ns_test_tmp"));

  std::string test_string = R"(
      globalThis.resultOne = undefined;
      ns.ls("/ns_test_tmp").
        then((result) => { globalThis.resultOne = result; }).
        catch((e) => { globalThis.resultOne = e;});
  )";
  ASSERT_TRUE(Eval(test_string));
  js_std_loop(ctx_->Get());
  test_string = R"(
      let res = globalThis.resultOne;
      if ("stack" in res) {
        throw res;
      }
      if (res.length != 1) {
          throw "Length != 1 in " + res;
      }
      if (res[0].name != ".")  {
          throw "Unexpected name " + res[0].name;
      }
  )";
  ASSERT_TRUE(Eval(test_string));

  constexpr const char* name = "/ns_test_tmp/tmp.XXXXXX";
  std::unique_ptr<char[]> buffer(new char[strlen(name) + 1]);
  strcpy(buffer.get(), name);
  int cfd = mkstemp(buffer.get());
  ASSERT_NE(cfd, -1);
  std::string filename = buffer.get();

  test_string = R"(
      globalThis.resultTwo = undefined;
      ns.ls("/ns_test_tmp").
        then((result) => { globalThis.resultTwo = result; }).
        catch((e) => { globalThis.resultTwo = e;});
  )";
  ASSERT_TRUE(Eval(test_string));
  js_std_loop(ctx_->Get());
  test_string = R"(
      let resTwo = globalThis.resultTwo;
      if ("stack" in resTwo) {
        throw resTwo;
      }
      let actualTwo = resTwo.map((x) => { return x.name; }).sort();
      if (actualTwo.length != 2) {
          throw "Length != 2 in " + actualTwo;
      }
      const expectedTwo = [".", ")" +
                filename + R"(".split("/")[2]].sort();
      for (let i = 0; i < expectedTwo.length; i++) {
          if (actualTwo[i] != expectedTwo[i]) {
              throw "Bad filenames: Expected " + expectedTwo[i] + ", got " + actualTwo[i];
          }
      }
  )";
  ASSERT_TRUE(Eval(test_string));
}

}  // namespace shell
