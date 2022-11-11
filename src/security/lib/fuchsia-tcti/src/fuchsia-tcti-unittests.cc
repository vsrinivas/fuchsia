// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.tpm/cpp/wire.h>

#include <memory>

#include <gtest/gtest.h>
#include <src/security/lib/fuchsia-tcti/include/fuchsia-tcti.h>

TEST(FuchsiaTctiTests, TpmSendArgumentValidation) {
  std::unique_ptr<opaque_ctx_t> context = fuchsia_tpm_init();
  std::vector<uint8_t> command(20, 0);
  int result = fuchsia_tpm_send(nullptr, 1, command.data(), command.size());
  ASSERT_NE(result, 0);
  result = fuchsia_tpm_send(context.get(), 1, nullptr, command.size());
  ASSERT_NE(result, 0);
  result = fuchsia_tpm_send(context.get(), 1, command.data(), 0);
  ASSERT_NE(result, 0);
  result =
      fuchsia_tpm_send(context.get(), 1, command.data(), fuchsia_tpm::wire::kMaxCommandLen + 1);
  ASSERT_NE(result, 0);
}

TEST(FuchsiaTctiTests, TpmRecvArgumentValidation) {
  std::unique_ptr<opaque_ctx_t> context = fuchsia_tpm_init();
  std::vector<uint8_t> command(20, 0);
  size_t no_bytes = 0;
  size_t bytes_read = fuchsia_tpm_recv(context.get(), command.data(), command.size());
  ASSERT_EQ(bytes_read, no_bytes);
  bytes_read = fuchsia_tpm_recv(nullptr, command.data(), command.size());
  ASSERT_EQ(bytes_read, no_bytes);
  bytes_read = fuchsia_tpm_recv(context.get(), nullptr, command.size());
  ASSERT_EQ(bytes_read, no_bytes);
  bytes_read = fuchsia_tpm_recv(context.get(), command.data(), 0);
  ASSERT_EQ(bytes_read, no_bytes);
}
