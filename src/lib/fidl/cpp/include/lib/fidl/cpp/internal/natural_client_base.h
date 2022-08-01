// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_BASE_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_BASE_H_

#include <lib/fidl/cpp/wire/client_base.h>

namespace fidl {
namespace internal {

// Common functionality to all |fidl::internal::NaturalClientImpl<Protocol>| sub-classes.
//
// Each |NaturalClientImpl| has access to a |client_base| through which they can
// send FIDL messages and register outstanding transactions.
//
// This class should only define protected methods, to keep the exposed
// interface for making FIDL calls clean.
class NaturalClientBase {
 public:
  explicit NaturalClientBase(fidl::internal::ClientBase* client_base) : client_base_(client_base) {}

 protected:
  fidl::internal::ClientBase& client_base() const { return *client_base_; }

 private:
  fidl::internal::ClientBase* client_base_;
};

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_BASE_H_
