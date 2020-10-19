// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <string>
#include <vector>

#include "launcher.h"

void Usage() {
  printf(
      "usage: fidl_echo_launcher [client_url] [server_url] [capability_names...]\n"
      "\n"
      "[client_url] url to the client component\n"
      "[server_url] url to the server component\n"
      "[capability_names...] names of capabilities that the server exposes to the client\n"
      "\n"
      "This tool launches the specified server and client, such that the protocol\n"
      "provided by the server is added to the client's launch environment. The\n"
      "user is responsible for ensuring that the client's cmx file allows the\n"
      "specified protocol in the client's sandbox. It returns the client's\n"
      "status code.\n");
}

int main(int argc, const char** argv) {
  if (argc < 4) {
    Usage();
    exit(1);
  }
  std::vector<std::string> capability_names;
  for (int i = 3; i < argc; i++) {
    capability_names.push_back(std::string(argv[i]));
  }
  return static_cast<int>(
      LaunchComponents(std::string(argv[1]), std::string(argv[2]), std::move(capability_names)));
}
