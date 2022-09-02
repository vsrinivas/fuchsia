// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_KEY_BAG_C_KEY_BAG_H_
#define SRC_LIB_STORAGE_KEY_BAG_C_KEY_BAG_H_

// Warning:
// This file was autogenerated by cbindgen.
// Do not modify this file manually.
//
// To update or regenerate the file, run
// > fx build
// > cd src/lib/storage/key-bag/c
// > fx gen-cargo //src/lib/storage/key-bag/c:_key_bag_rustc_static
// > cbindgen $PWD/ -o $PWD/key_bag.h -c cbindgen.toml
//
// clang-format off


#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>
#include <zircon/types.h>

namespace key_bag {

static const uintptr_t AES256_KEY_SIZE = 32;

// Manages the persistence of a KeyBag.
//
// All operations on the keybag are atomic.
struct KeyBagManager;

struct Aes256Key {
  uint8_t _0[AES256_KEY_SIZE];
};

extern "C" {

// Creates a `KeyBagManager` by opening or creating 'path', and returns an opaque pointer to it.
//
// # Safety
//
// The passed arguments might point to invalid memory.
zx_status_t keybag_create(const char *path, KeyBagManager **out);

// Deallocates a previously created `KeyBagManager`.
//
// # Safety
//
// The passed arguments might point to invalid memory, or an object we didn't allocate.
void keybag_destroy(KeyBagManager *keybag);

// Generates and stores a wrapped key in the key bag.
//
// # Safety
//
// The passed arguments might point to invalid memory.
zx_status_t keybag_new_key(KeyBagManager *keybag, uint16_t slot, const Aes256Key *wrapping_key);

// Removes the key at the given slot from the key bag.
//
// # Safety
//
// The passed arguments might point to invalid memory.
zx_status_t keybag_remove_key(KeyBagManager *keybag, uint16_t slot);

// Unwraps all keys which can be unwrapped with |wrapping_key|
//
// # Safety
//
// The passed arguments might point to invalid memory.
zx_status_t keybag_unwrap_key(KeyBagManager *keybag,
                              uint16_t slot,
                              const Aes256Key *wrapping_key,
                              Aes256Key *out_key);

} // extern "C"

} // namespace key_bag

#endif // SRC_LIB_STORAGE_KEY_BAG_C_KEY_BAG_H_
