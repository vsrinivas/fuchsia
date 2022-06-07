// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <iostream>
// [END imports]

#include "examples/components/echo/cpp/echo_component.h"

// [START main]
int main(int argc, const char* argv[], char* envp[]) {
  syslog::SetTags({"echo"});
  // Read program arguments, and exclude the binary name in argv[0]
  std::vector<std::string> arguments;
  for (int i = 1; i < argc; i++) {
    arguments.push_back(argv[i]);
  }

  // Include environment variables
  const char* favorite_animal = std::getenv("FAVORITE_ANIMAL");
  arguments.push_back(favorite_animal);

  // Print a greeting to syslog
  FX_SLOG(INFO, "Hello", KV("greeting", echo::greeting(arguments).c_str()));

  return 0;
}
// [END main]
