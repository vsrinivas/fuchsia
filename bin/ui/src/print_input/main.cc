// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

class PrintInput {
 public:
  PrintInput();
  ~PrintInput();

  void OnInitialize();

 private:
  mozart::input::InputInterpreter interpreter_;
  std::unique_ptr<mozart::input::InputReader> reader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PrintInput);
};

PrintInput::PrintInput() {}

PrintInput::~PrintInput() {}

void PrintInput::OnInitialize() {
  interpreter_.RegisterCallback(
      [this](mozart::EventPtr event) { FTL_LOG(INFO) << *(event.get()); });

  reader_.reset(new mozart::input::InputReader(&interpreter_));
  reader_->Start();
}

int main(int argc, char** argv) {
  mtl::MessageLoop message_loop;
  PrintInput input_app;
  message_loop.task_runner()->PostTask(
      [&input_app]() { input_app.OnInitialize(); });
  message_loop.Run();
  return 0;
}
