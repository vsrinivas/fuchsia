// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/services/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <iostream>

struct Account {
  // Account owner's name
  std::string name;
  // Account balance in cents
  int64_t balance;
};

// Implementation of the `fuchsia.examples.services/ReadOnlyAccount` protocol
class ProtectedAccount : public fuchsia::examples::services::ReadOnlyAccount {
 public:
  explicit ProtectedAccount(Account account) : account_(account) {}

  void GetOwner(GetOwnerCallback callback) override { callback(account_.name); }
  void GetBalance(GetBalanceCallback callback) override { callback(account_.balance); }

 private:
  Account account_;
};

// Implementation of the `fuchsia.examples.services/ReadWriteAccount` protocol
class OpenAccount : public fuchsia::examples::services::ReadWriteAccount {
 public:
  explicit OpenAccount(Account account) : account_(account) {}

  void GetOwner(GetOwnerCallback callback) override { callback(account_.name); }
  void GetBalance(GetBalanceCallback callback) override { callback(account_.balance); }
  void Debit(int64_t amount, DebitCallback callback) override {
    if (account_.balance >= amount) {
      account_.balance -= amount;
      callback(true);
    } else {
      callback(false);
    }
    FX_SLOG(INFO, "Account balance updated: ", KV("balance", account_.balance));
  }
  void Credit(int64_t amount, CreditCallback callback) override {
    account_.balance += amount;
    callback();
    FX_SLOG(INFO, "Account balance updated: ", KV("balance", account_.balance));
  }

 private:
  Account account_;
};

int main(int argc, const char* argv[], char* envp[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Read program arguments and construct the account
  if (argc < 3) {
    FX_SLOG(ERROR, "Invalid number of arguments.");
    return -1;
  }
  auto name = argv[1];
  auto balance = atoi(argv[2]);
  Account user_account = {.name = name, .balance = balance};
  FX_SLOG(INFO, "Starting bank account provider", KV("name", user_account.name.c_str()),
          KV("balance", user_account.balance));

  // Set up handler for BankAccount service
  sys::ServiceHandler handler;
  fuchsia::examples::services::BankAccount::Handler bank_service(&handler);

  // Add protocol implementations to the service handler
  ProtectedAccount protected_account(user_account);
  fidl::BindingSet<fuchsia::examples::services::ReadOnlyAccount> read_only_bindings;
  bank_service.add_read_only(read_only_bindings.GetHandler(&protected_account));

  OpenAccount open_account(user_account);
  fidl::BindingSet<fuchsia::examples::services::ReadWriteAccount> read_write_bindings;
  bank_service.add_read_write(read_write_bindings.GetHandler(&open_account));

  // Publish the service to the outgoing directory
  context->outgoing()->AddService<fuchsia::examples::services::BankAccount>(std::move(handler));

  loop.Run();
  return 0;
}
