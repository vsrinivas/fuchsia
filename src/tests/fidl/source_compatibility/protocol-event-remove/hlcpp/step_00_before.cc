// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>

#include <fidl/test/protocoleventremove/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::protocoleventremove;

// [START contents]
void expectEvents(fidl_test::ExamplePtr* client) {
  client->events().OnExistingEvent = []() {};
  client->events().OnOldEvent = []() {};
}

void sendEvents(fidl::Binding<fidl_test::Example>* server) {
  server->events().OnExistingEvent();
  server->events().OnOldEvent();
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
