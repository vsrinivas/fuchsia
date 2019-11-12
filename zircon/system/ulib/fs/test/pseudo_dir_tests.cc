// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/vfs_types.h>
#include <unittest/unittest.h>

namespace {

class DirentChecker {
 public:
  DirentChecker(const void* buffer, size_t length)
      : current_(reinterpret_cast<const uint8_t*>(buffer)), remaining_(length) {}

  bool ExpectEnd() {
    BEGIN_HELPER;

    EXPECT_EQ(0u, remaining_);

    END_HELPER;
  }

  bool ExpectEntry(const char* name, uint32_t vtype) {
    BEGIN_HELPER;

    ASSERT_NE(0u, remaining_);
    auto entry = reinterpret_cast<const vdirent_t*>(current_);
    size_t entry_size = entry->size + sizeof(vdirent_t);
    ASSERT_GE(remaining_, entry_size);
    current_ += entry_size;
    remaining_ -= entry_size;
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(name),
                    reinterpret_cast<const uint8_t*>(entry->name), strlen(name), "name");
    EXPECT_EQ(VTYPE_TO_DTYPE(vtype), entry->type);

    END_HELPER;
  }

 private:
  const uint8_t* current_;
  size_t remaining_;
};

bool TestPseudoDir() {
  BEGIN_TEST;

  auto dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  auto subdir = fbl::AdoptRef<fs::Vnode>(new fs::PseudoDir());
  auto file1 = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());
  auto file2 = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());

  // add entries
  EXPECT_EQ(ZX_OK, dir->AddEntry("subdir", subdir));
  EXPECT_EQ(ZX_OK, dir->AddEntry("file1", file1));
  EXPECT_EQ(ZX_OK, dir->AddEntry("file2", file2));
  EXPECT_EQ(ZX_OK, dir->AddEntry("file2b", file2));

  // try to add duplicates
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, dir->AddEntry("subdir", subdir));
  EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, dir->AddEntry("file1", subdir));

  // remove entries
  EXPECT_EQ(ZX_OK, dir->RemoveEntry("file2"));
  EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->RemoveEntry("file2"));

  // open as directory
  fs::VnodeConnectionOptions options_directory;
  options_directory.flags.directory = true;
  fbl::RefPtr<fs::Vnode> redirect;
  auto validated_options = dir->ValidateOptions(options_directory);
  EXPECT_TRUE(validated_options.is_ok());
  EXPECT_EQ(ZX_OK, dir->Open(validated_options.value(), &redirect));
  EXPECT_NULL(redirect);

  // get attributes
  fs::VnodeAttributes attr;
  EXPECT_EQ(ZX_OK, dir->GetAttributes(&attr));
  EXPECT_EQ(V_TYPE_DIR | V_IRUSR, attr.mode);
  EXPECT_EQ(1, attr.link_count);

  // lookup entries
  fbl::RefPtr<fs::Vnode> node;
  EXPECT_EQ(ZX_OK, dir->Lookup(&node, "subdir"));
  EXPECT_EQ(subdir.get(), node.get());
  EXPECT_EQ(ZX_OK, dir->Lookup(&node, "file1"));
  EXPECT_EQ(file1.get(), node.get());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&node, "file2"));
  EXPECT_EQ(ZX_OK, dir->Lookup(&node, "file2b"));
  EXPECT_EQ(file2.get(), node.get());

  // readdir
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t length;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    DirentChecker dc(buffer, length);
    EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEntry("subdir", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEntry("file1", V_TYPE_FILE));
    EXPECT_TRUE(dc.ExpectEntry("file2b", V_TYPE_FILE));
    EXPECT_TRUE(dc.ExpectEnd());
  }

  // readdir with small buffer
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[2 * sizeof(vdirent) + 13];
    size_t length;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    DirentChecker dc(buffer, length);
    EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEntry("subdir", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEnd());

    // readdir again
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    DirentChecker dc1(buffer, length);
    EXPECT_TRUE(dc1.ExpectEntry("file1", V_TYPE_FILE));
    EXPECT_TRUE(dc1.ExpectEntry("file2b", V_TYPE_FILE));
    EXPECT_TRUE(dc1.ExpectEnd());
  }

  // test removed entries do not appear in readdir or lookup
  dir->RemoveEntry("file1");
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t length;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    DirentChecker dc(buffer, length);
    EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEntry("subdir", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEntry("file2b", V_TYPE_FILE));
    EXPECT_TRUE(dc.ExpectEnd());
  }
  EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup(&node, "file1"));

  // remove all entries
  dir->RemoveAllEntries();

  // readdir again
  {
    fs::vdircookie_t cookie = {};
    uint8_t buffer[4096];
    size_t length;
    EXPECT_EQ(dir->Readdir(&cookie, buffer, sizeof(buffer), &length), ZX_OK);
    DirentChecker dc(buffer, length);
    EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
    EXPECT_TRUE(dc.ExpectEnd());
  }

  // FIXME(ZX-1186): Can't unittest watch/notify (hard to isolate right now).

  END_TEST;
}

bool TestRejectOpenFlagNotDirectory() {
  BEGIN_TEST;

  auto dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  auto result = dir->ValidateOptions(fs::VnodeConnectionOptions::ReadOnly().set_not_directory());
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(ZX_ERR_NOT_FILE, result.error());

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(pseudo_dir_tests)
RUN_TEST(TestPseudoDir)
RUN_TEST(TestRejectOpenFlagNotDirectory)
END_TEST_CASE(pseudo_dir_tests)
