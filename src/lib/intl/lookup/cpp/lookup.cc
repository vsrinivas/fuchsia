// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lookup.h"

#include <lib/fit/result.h>

#include <memory>
#include <string>
#include <vector>

namespace intl {

namespace {

std::vector<char*> AsCStrings(const std::vector<std::string>& strings) {
  std::vector<char*> cstrings;
  cstrings.reserve(strings.size());
  for (const auto& id : strings) {
    cstrings.push_back(const_cast<char*>(id.c_str()));
  }
  return cstrings;
}

}  // namespace

fit::result<std::unique_ptr<Lookup>, Lookup::Status> Lookup::NewForTest(
    const std::vector<std::string>& locale_ids, intl_lookup_ops_t ops) {
  auto status = Lookup::Status::OK;
  std::vector<char*> cstrings = AsCStrings(locale_ids);
  auto* raw_lookup =
      ops.op_new(locale_ids.size(), &cstrings[0], reinterpret_cast<int8_t*>(&status));
  if (status != Lookup::Status::OK) {
    return fit::error(status);
  }
  // make_unique does not work here since Lookup constructor is private.
  std::unique_ptr<Lookup> impl(new Lookup(raw_lookup, ops));
  return fit::ok(std::move(impl));
}

fit::result<std::unique_ptr<Lookup>, Lookup::Status> Lookup::New(
    const std::vector<std::string>& locale_ids) {
  return Lookup::NewForTest(locale_ids, intl_lookup_ops_t{
                                            .op_new = intl_lookup_new,
                                            .op_delete = intl_lookup_delete,
                                            .op_string = intl_lookup_string,
                                        });
}

Lookup::Lookup(intl_lookup_t* impl, intl_lookup_ops_t ops_for_test)
    : ops_(ops_for_test), impl_(impl) {}

Lookup::~Lookup() {
  ops_.op_delete(impl_);
  impl_ = nullptr;
}

fit::result<std::string_view, Lookup::Status> Lookup::String(uint64_t message_id) {
  auto status = Lookup::Status::OK;
  char* result = ops_.op_string(impl_, message_id, reinterpret_cast<int8_t*>(&status));
  if (status != Lookup::Status::OK) {
    return fit::error(status);
  }
  return fit::ok(std::string_view(result));
}

};  // namespace intl
