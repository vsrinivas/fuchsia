// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_TESTS_EFI_TESTS_H_
#define SRC_LIB_ZBITL_TESTS_EFI_TESTS_H_

#include <lib/efi/testing/stdio_file_protocol.h>
#include <lib/zbitl/efi.h>

#include "stdio-tests.h"

// This piggy-backs on StdioTestTraits to do the actual storage by using the
// efi::StdioFileProtocol wrapper as a bridge.

struct EfiTestTraits {
  using storage_type = efi_file_protocol*;
  using payload_type = uint64_t;

  static constexpr bool kExpectExtensibility = true;
  static constexpr bool kExpectOneShotReads = false;
  static constexpr bool kExpectUnbufferedReads = true;
  static constexpr bool kExpectUnbufferedWrites = false;

  struct Context {
    storage_type TakeStorage() { return file_.protocol(); }

    void Reify() { file_ = efi::StdioFileProtocol{std::exchange(stdio_.storage_, nullptr)}; }

    efi::StdioFileProtocol file_;
    StdioTestTraits::Context stdio_;
  };

  static void Create(size_t size, Context* context) {
    StdioTestTraits::Create(size, &context->stdio_);
    context->Reify();
  }

  static void Create(fbl::unique_fd fd, size_t size, Context* context) {
    StdioTestTraits::Create(std::move(fd), size, &context->stdio_);
    context->Reify();
  }

  static void Read(storage_type storage, payload_type payload, size_t size, Bytes* contents) {
    StdioTestTraits::Read(efi::StdioFileProtocol::FromProtocol(storage).stdio_file(),
                          static_cast<long int>(payload), size, contents);
  }

  static void Write(storage_type storage, uint32_t offset, const Bytes& data) {
    StdioTestTraits::Write(efi::StdioFileProtocol::FromProtocol(storage).stdio_file(), offset,
                           data);
  }

  static void ToPayload(storage_type storage, uint32_t offset, payload_type& payload) {
    payload = static_cast<payload_type>(offset);
  }
};

#endif  // SRC_LIB_ZBITL_TESTS_EFI_TESTS_H_
