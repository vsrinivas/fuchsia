// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <thread>

#include "garnet/public/lib/fxl/macros.h"

namespace debug_ipc {
class MessageLoop;
}

namespace zxdb {

class SymbolsImpl;
class SystemSymbols;

// SystemSymbols runs on the background thread. This class provides a proxy
// to that thread to help avoid threading mistakes.
//
// SYMBOLS THREAD
//              SystemSymbols <------------------ ProcessSymbols
//                    ^                                 ^
// ...................|.................................|..................
// MAIN THREAD        |                                 |
//             SystemSymbolsProxy <--------------- SymbolsImpl
//                    ^                                 ^
//                    |                                 |
//                SystemImpl <---> TargetImpl <---> ProcessImpl
//
class SystemSymbolsProxy {
 public:
  SystemSymbolsProxy();
  ~SystemSymbolsProxy();

  debug_ipc::MessageLoop* main_loop() { return main_loop_; }
  debug_ipc::MessageLoop* symbol_loop() { return symbol_loop_.get(); }

  // Schedules a load of the default ids.txt symbol mapping file, and
  // asynchronously calls the callback. ids_loaded will be filled in according
  // to whether the file could be loaded, and the message will describe what
  // happened on both success or failure.
  void Init(
      std::function<void(bool ids_loaded, const std::string& msg)> callback);

 private:
  // The SymbolsImpl can read the symbols_ pointer so it can post to it.
  friend SymbolsImpl;

  debug_ipc::MessageLoop* main_loop_;

  // Everything in the "symbols" subdirectory runs on this thread + loop.
  std::unique_ptr<std::thread> symbol_thread_;
  std::unique_ptr<debug_ipc::MessageLoop> symbol_loop_;

  // Must only be accessed on the symbol_loop_.
  std::unique_ptr<SystemSymbols> symbols_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemSymbolsProxy);
};

}  // namespace zxdb
