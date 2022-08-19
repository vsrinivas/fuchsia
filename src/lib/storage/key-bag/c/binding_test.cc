// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include <string>

#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/storage/key-bag/c/key_bag.h"
#include "zircon/errors.h"

namespace key_bag {
namespace {

TEST(KeyBagTest, CreateEmptyKeyBag) {
  files::ScopedTempDir dir;
  std::string temp_file;
  ASSERT_TRUE(dir.NewTempFile(&temp_file));
  KeyBagManager* kb;
  ASSERT_EQ(keybag_create(temp_file.c_str(), &kb), ZX_OK);
  auto cleanup = fit::defer([&]() { keybag_destroy(kb); });
}

TEST(KeyBagTest, AddRemoveKey) {
  files::ScopedTempDir dir;
  std::string temp_file;
  ASSERT_TRUE(dir.NewTempFile(&temp_file));
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_create(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_destroy(kb); });

    Aes256Key key{._0{0}};
    ASSERT_EQ(keybag_new_key(kb, 0, &key), ZX_OK);
  }
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_create(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_destroy(kb); });

    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_OK);
    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_ERR_NOT_FOUND);
  }
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_create(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_destroy(kb); });

    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_ERR_NOT_FOUND);
  }
}

}  // namespace
}  // namespace key_bag
