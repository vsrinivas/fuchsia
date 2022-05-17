// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_H_
#define EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_H_

#include <fuchsia/examples/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

class Profile : public fuchsia::examples::diagnostics::Profile {
 public:
  void AddBinding(fidl::InterfaceRequest<fuchsia::examples::diagnostics::Profile> channel) {
    bindings_.AddBinding(this, std::move(channel), dispatcher_);
  }

  void AddReaderBinding(
      fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileReader> channel) {
    reader_.AddBinding(std::move(channel));
  }

  explicit Profile(async_dispatcher_t* dispatcher, std::string);

  ~Profile() override;

  void SetName(std::string name) override;

  void GetName(GetNameCallback callback) override;

  void AddBalance(int64_t amount) override;

  void WithdrawBalance(int64_t amount, WithdrawBalanceCallback callback) override;

  void GetBalance(GetBalanceCallback callback) override;

 private:
  class Reader : public fuchsia::examples::diagnostics::ProfileReader {
   public:
    void AddBinding(fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileReader> channel) {
      bindings_.AddBinding(this, std::move(channel), parent_->dispatcher_);
    }

    explicit Reader(Profile* parent);
    ~Reader() override;

    void GetName(GetNameCallback callback) override;

    void GetBalance(GetBalanceCallback callback) override;

   private:
    fidl::BindingSet<fuchsia::examples::diagnostics::ProfileReader> bindings_;
    Profile* parent_;
  };

  fidl::BindingSet<fuchsia::examples::diagnostics::Profile> bindings_;
  std::string name_;
  int64_t balance_;
  std::string filepath_;
  Reader reader_;
  async_dispatcher_t* dispatcher_;
};

#endif  // EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_H_
