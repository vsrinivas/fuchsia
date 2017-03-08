// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

std::ostream& operator<<(std::ostream& os,
                         const mozart::input::InputDevice& value) {
  os << "{InputDevice#" << value.id() << ":";
  if (value.has_keyboard()) {
    os << "KEYBOARD:";
  }
  if (value.has_mouse()) {
    os << "MOUSE:";
  }
  if (value.has_stylus()) {
    os << "STYLUS:";
  }
  if (value.has_touchscreen()) {
    os << "TOUCHSCREEN:";
  }
  os << "/dev/class/input/" << value.name() << "}";
  return os;
}

class PrintInput : public mozart::input::InterpreterListener {
 public:
  PrintInput() : weak_ptr_factory_(this) {
    mozart::Size size;
    size.width = 1.0;
    size.height = 1.0;
    interpreter_.RegisterDisplay(size);
    interpreter_.SetListener(weak_ptr_factory_.GetWeakPtr());
    reader_ = std::make_unique<mozart::input::InputReader>(&interpreter_);
    reader_->Start();
  }
  ~PrintInput() {}

 private:
  // |InputInterpreterListener|:
  void OnEvent(mozart::InputEventPtr event) { FTL_LOG(INFO) << *(event.get()); }

  void OnDeviceAdded(const mozart::input::InputDevice* device) {
    FTL_LOG(INFO) << *device << " Added";
  }

  void OnDeviceRemoved(const mozart::input::InputDevice* device) {
    FTL_LOG(INFO) << *device << " Removed";
  }

  mozart::input::InputInterpreter interpreter_;
  std::unique_ptr<mozart::input::InputReader> reader_;
  ftl::WeakPtrFactory<PrintInput> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PrintInput);
};

}  // namespace

int main(int argc, char** argv) {
  mtl::MessageLoop message_loop;
  PrintInput app;
  message_loop.Run();
  return 0;
}
