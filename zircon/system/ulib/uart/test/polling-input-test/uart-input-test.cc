// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/qemu.h>
#include <stdio.h>
#include <stdlib.h>

#include <ktl/array.h>
#include <ktl/string_view.h>
#include <phys/main.h>
#include <phys/stdio.h>
#include <phys/uart.h>

#include "test-main.h"

namespace {

// LINT.IfChange
constexpr std::string_view kSerialReady = "UartInputReady";
constexpr std::string_view kSerialCommand = "RandomString1234!";
// LINT.ThenChange(./uart_input_host_test.go)

// Will write to the uart
// "Ready for Input"
// Will read "RandomString" from the uart.
// Returns true if all operations succeeded.
bool UartInputTest(UartDriver& uart) {
  // Read "RandomString\n"
  ktl::array<char, 20> input = {};

  uart.Visit([&](auto& driver) {
    size_t i = 0;
    while (i < input.size() - 1) {
      auto c = driver.Read();
      if (!c) {
        continue;
      }
      if (*c == '\n' || *c == '\r') {
        break;
      }
      input[i] = static_cast<char>(*c);
      i++;
    }
  });

  if (ktl::string_view(input.data()) != kSerialCommand) {
    printf("uart-input-test: Unexpected input: %s instead of %*s.\n", input.data(),
           static_cast<int>(kSerialCommand.length()), kSerialCommand.data());
    return false;
  }

  printf("uart-input-test: Received %s\n", input.data());
  return true;
}

}  // namespace

int TestMain(void* zbi, arch::EarlyTicks ticks) {
  printf("uart-input-test: %*s\n", static_cast<int>(kSerialReady.length()), kSerialReady.data());
  // Run the test.
  return UartInputTest(GetUartDriver()) ? 0 : 1;
}
