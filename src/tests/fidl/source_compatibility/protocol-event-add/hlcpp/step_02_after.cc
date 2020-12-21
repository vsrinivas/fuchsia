// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/protocoleventadd/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::protocoleventadd;

// [START contents]
void expectEvents(fidl_test::ExamplePtr* client) {
  client->events().OnExistingMethod = []() {};
}

void sendEvents(fidl::Binding<fidl_test::Example>* server) { server->events().OnExistingMethod(); }

// [END contents]

int main(int argc, const char** argv) { return 0; }
