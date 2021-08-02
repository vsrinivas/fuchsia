// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TESTING_FIDL_FROBINATOR_IMPL_H_
#define TESTING_FIDL_FROBINATOR_IMPL_H_

#include <lib/fit/function.h>

#include <string>
#include <vector>

#include <fidl/test/frobinator/cpp/fidl.h>

namespace fidl {
namespace test {

class FrobinatorImpl : public fidl::test::frobinator::Frobinator {
 public:
  FrobinatorImpl(fit::closure on_destroy = [] {});
  ~FrobinatorImpl();

  uint32_t send_basic_union_received_value_ = 0;
  std::vector<std::string> frobs;
  std::vector<std::string> grobs;
  fit::closure on_destroy_;

  void Frob(std::string value) override;
  void Grob(std::string value, GrobCallback callback) override;
  void Fail(bool fail, FailCallback callback) override;
  void FailHard(bool fail, FailHardCallback callback) override;
  void FailHardest(bool fail, FailHardestCallback callback) override;
  void SendEventHandle(zx::event event) override;
  void SendProtocol(fidl::InterfaceHandle<fidl::test::frobinator::EmptyProtocol> ep) override;
  void SendBasicUnion(fidl::test::frobinator::BasicUnion u) override;
};

}  // namespace test
}  // namespace fidl

#endif  // TESTING_FIDL_FROBINATOR_IMPL_H_
