// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_TEST_FROBINATOR_IMPL_H_
#define LIB_FIDL_CPP_TEST_FROBINATOR_IMPL_H_

#include <fidl/test/frobinator/cpp/fidl.h>

#include <string>
#include <vector>

namespace fidl {
namespace test {

class FrobinatorImpl : public fidl::test::frobinator::Frobinator {
 public:
  FrobinatorImpl();
  ~FrobinatorImpl();

  std::vector<std::string> frobs;
  std::vector<std::string> grobs;

  void Frob(StringPtr value) override;
  void Grob(StringPtr value, GrobCallback callback) override;
};

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_TEST_FROBINATOR_IMPL_H_
