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

TEST(KeyBagTest, OpenEmptyKeyBag) {
  files::ScopedTempDir dir;
  std::string temp_file;
  ASSERT_TRUE(dir.NewTempFile(&temp_file));
  KeyBagManager* kb;
  ASSERT_EQ(keybag_open(temp_file.c_str(), &kb), ZX_OK);
  auto cleanup = fit::defer([&]() { keybag_close(kb); });
}

TEST(KeyBagTest, AddRemoveKey) {
  files::ScopedTempDir dir;
  std::string temp_file;
  ASSERT_TRUE(dir.NewTempFile(&temp_file));
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_open(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_close(kb); });

    uint8_t raw[AES256_KEY_SIZE];
    WrappingKey wrap;
    ASSERT_EQ(keybag_create_aes256_wrapping_key(raw, sizeof(raw), &wrap), ZX_OK);
    Aes256Key out;
    ASSERT_EQ(keybag_new_key(kb, 0, &wrap, &out), ZX_OK);
  }
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_open(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_close(kb); });

    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_OK);
    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_ERR_NOT_FOUND);
  }
  {
    KeyBagManager* kb;
    ASSERT_EQ(keybag_open(temp_file.c_str(), &kb), ZX_OK);
    auto cleanup = fit::defer([&]() { keybag_close(kb); });

    ASSERT_EQ(keybag_remove_key(kb, 0), ZX_ERR_NOT_FOUND);
  }
}

}  // namespace
}  // namespace key_bag
