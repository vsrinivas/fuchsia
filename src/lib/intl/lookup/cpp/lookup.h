// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides an API for looking up localized message strings.
//
// Example use:
//
// ```
// std::vector<std::string> locale_ids = {"nl-NL"};
// auto result = Lookup::New(locale_ids);
// if (result.is_error()) {
//   // handle error
//   return;
// }
// auto lookup = result.value();
// auto lookup_result = lookup.string(42);
// if (lookup_result.is_error()) {
//   // handle error
//   return;
// }
// std::string_view message = lookup_result.value();
// // Use `message`.
// ```

#ifndef SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_
#define SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_

#include <lib/fit/result.h>

#include <memory>
#include <string>

#include "gtest/gtest_prod.h"
#include "src/lib/intl/lookup/rust/lookup.h"

namespace intl {

// The C++ API used to look up localized messages by their unique message ID.
// See the top of this header file for use examples.
class Lookup {
 public:
  ~Lookup();

  // Error codes reported by New() and Lookup() methods.
  enum class Status : int8_t {
    // No error.
    OK = 0,
    // The value requested is not available.
    UNAVAILABLE = 1,
    // The argument passed in by the user is not valid.
    ARGUMENT_ERROR = 2,
  };

  // Makes a new lookup object, which contains information about the passed-in
  // locales.  At present, if any one of the locales is not present verbatim,
  // an error is returned.
  //
  // Errors:
  //   - UNAVAILABLE: one of the requested locale IDs is not avalable for use.
  //   - ARGUMENT_ERROR: the locales ids are malfomed, e.g. nonexistent, or
  //     not a valid UTF8 encoding of the locale ID.
  static fit::result<std::unique_ptr<Lookup>, Lookup::Status> New(
      const std::vector<std::string>& locale_ids);

  // Looks up the message by its unique `message_id`.
  //
  // Errors:
  //   - UNAVAILABLE: the requested message ID is not present in the loaded
  //     resource bundle.
  fit::result<std::string_view, Lookup::Status> String(uint64_t message_id);

  static fit::result<std::unique_ptr<Lookup>, Lookup::Status> NewForTest(
      const std::vector<std::string>& locale_ids, const intl_lookup_ops_t ops);

 private:
  FRIEND_TEST(Intl, CreateError);
  FRIEND_TEST(Intl, LookupReturnValues);

  // Injects fake operations for testing. Takes ownership of impl.
  Lookup(intl_lookup_t* impl, intl_lookup_ops_t ops_for_test);

  // Operations used to access the implementation type from the C ABI,
  // int_lookup_t*.  The ops need to vary only for tests.
  const intl_lookup_ops_t ops_;

  // Owned by this class. Never null for a properly initialized class.
  intl_lookup_t* impl_ = nullptr;

  Lookup(const Lookup&) = delete;
  Lookup(const Lookup&&) = delete;
  Lookup() = delete;
};

};  // namespace intl

#endif  // SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_
