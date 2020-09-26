// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fbl/vector.h>
#include <fs/dir_test_util.h>
#include <fs/lazy_dir.h>
#include <fs/pseudo_file.h>
#include <fs/vfs.h>
#include <zxtest/zxtest.h>

namespace {

class TestLazyDirHelper : public fs::LazyDir {
 public:
  struct TestContent {
    uint64_t id;
    fbl::String name;
  };

  void GetContents(LazyEntryVector* out_vector) override {
    out_vector->reserve(contents_.size());
    for (const auto& content : contents_) {
      out_vector->push_back({content.id, content.name, V_TYPE_FILE});
    }
  }

  zx_status_t GetFile(fbl::RefPtr<fs::Vnode>* out, uint64_t id, fbl::String name) override {
    last_output_file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());
    *out = last_output_file;
    last_id = id;
    last_name = name;
    return ZX_OK;
  }

  void AddContent(TestContent content) { contents_.push_back(std::move(content)); }

  fbl::RefPtr<fs::Vnode> last_output_file;
  uint64_t last_id;
  fbl::String last_name;

 private:
  fbl::Vector<TestContent> contents_;
};

TEST(LazyDir, ApiTest) {
  TestLazyDirHelper test;

  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t len;

    EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
    fs::DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEnd();
  }

  test.AddContent({1, "test"});
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t len;

    EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
    fs::DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("test", V_TYPE_FILE);
    dc.ExpectEnd();

    fbl::RefPtr<fs::Vnode> out;
    EXPECT_EQ(test.Lookup("test", &out), ZX_OK);
    EXPECT_EQ(1, test.last_id);
    EXPECT_TRUE(strcmp("test", test.last_name.c_str()) == 0);
    EXPECT_EQ(out.get(), test.last_output_file.get());

    EXPECT_EQ(test.Lookup("test2", &out), ZX_ERR_NOT_FOUND);
  }
  test.AddContent({33, "aaaa"});
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t len;

    EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
    fs::DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("test", V_TYPE_FILE);
    dc.ExpectEntry("aaaa", V_TYPE_FILE);
    dc.ExpectEnd();

    fbl::RefPtr<fs::Vnode> out;
    EXPECT_EQ(test.Lookup("aaaa", &out), ZX_OK);
    EXPECT_EQ(33, test.last_id);
    EXPECT_TRUE(strcmp("aaaa", test.last_name.c_str()) == 0);
    EXPECT_EQ(out.get(), test.last_output_file.get());
  }
  {
    // Ensure manually setting cookie past entries excludes them, but leaves "."
    fs::vdircookie_t cookie = {};
    cookie.n = 30;
    uint8_t buffer[4096];
    size_t len;

    EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
    fs::DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("aaaa", V_TYPE_FILE);
    dc.ExpectEnd();

    // Expect that "." is missing when reusing the cookie.
    EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
    dc = fs::DirentChecker(buffer, len);
    dc.ExpectEnd();
  }
}

}  // namespace
