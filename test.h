// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/debug.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

typedef std::function<void(mojo::Shell*)> TestRoutine;

struct Test {
  Test(const std::string& name, TestRoutine run) : name(name), run(run) {}
  const std::string name;
  const TestRoutine run;
};

void Yield();

template <typename Predicate>
void WaitUntil(Predicate until) {
  do {
    Yield();
  } while (!until());
}

void StartComponent(mojo::Shell* shell, const std::string& url);
void Sleep(unsigned int millis);

// Pauses the main thread to allow Mojo messages to propagate. It will wait
// until no new debuggable have been launched for 500 ms, for a max of 2
// seconds.
void Pause();
