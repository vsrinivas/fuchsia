// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides an API for looking up localized message strings.
//
// Localized message strings are strings which have a different value based on
// the locale that is supplied as the context of the lookup.  So, if your locale
// is, say "en", your `lookup.string(42)` may return `Hello world`, but if your
// locale is "nl", your `lookup.string(42)` may return `Groetjes, wereld`.
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
//
// Note, almost all of the implementation of this class is
// in fact in rust code behind a FFI-able C ABI.  One should
// normally not need to look under the hood if all you want is to test
// the interaction with your code with this library.  This means, if it does
// not support something you need, filing a bug at https://fxbug.dev may be
// the fastest way to get what you need.

#ifndef SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_
#define SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_

#include <lib/fpromise/result.h>

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
    // Some internal error happened.  Consult logs for details.
    INTERNAL = 111,
  };

  // Makes a new lookup object, which contains information about the passed-in
  // locales.  At present, if any one of the locales is not present verbatim,
  // an error is returned.
  //
  // Errors:
  //   - UNAVAILABLE: one of the requested locale IDs is not avalable for use.
  //   - ARGUMENT_ERROR: the locales ids are malfomed, e.g. nonexistent, or
  //     not a valid UTF8 encoding of the locale ID.
  static fpromise::result<std::unique_ptr<Lookup>, Lookup::Status> New(
      const std::vector<std::string>& locale_ids);

  // Looks up the message by its unique `message_id`.
  //
  // Errors:
  //   - UNAVAILABLE: the requested message ID is not present in the loaded
  //     resource bundle.
  fpromise::result<std::string_view, Lookup::Status> String(uint64_t message_id);

  // Instantiates a fake Lookup instance, which is useful for tests that don't
  // want to make a full end-to-end localization setup.
  //
  // The fake is simplistic and it is the intention that it provides you with
  // some default fake behaviors.  The behaviors are as follows at the moment,
  // and more could be added if needed.
  //
  // - If `locale_ids` contains the string `en-US`, the constructor will
  //   return  UNAVAILABLE.
  // - If the message ID pased to `Lookup::String()` is exactly 1, the fake
  //   returns `Hello, {person}!`, so that you can test 1-parameter formatting.
  // - Otherwise, for an even mesage ID it returns "Hello world!", or for
  //   an odd message ID returns UNAVAILABLE.
  //
  // The implementation of the fake itself is done in rust behind a FFI ABI,
  // see the package //src/lib/intl/lookup/rust for details.
  static fpromise::result<std::unique_ptr<Lookup>, Lookup::Status> NewForTest(
      const std::vector<std::string>& locale_ids);

  // Same as above, except allows you to pass in custom behavior operations
  // for the fake and affect its behavior.  As a user of this library you should
  // normally never need to use this particular constructor.  If you need
  // special behavior, consider filing a feature request instead to component
  // "I18N>Localization" at https://fxbug.dev.
  static fpromise::result<std::unique_ptr<Lookup>, Lookup::Status> NewForTest(
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

}  // namespace intl

#endif  // SRC_LIB_INTL_LOOKUP_CPP_LOOKUP_H_
