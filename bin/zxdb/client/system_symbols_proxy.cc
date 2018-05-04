// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system_symbols_proxy.h"

#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"

namespace zxdb {

SystemSymbolsProxy::SystemSymbolsProxy()
    : main_loop_(debug_ipc::MessageLoop::Current()),
      symbols_(std::make_unique<SystemSymbols>()) {
  symbol_loop_ = std::make_unique<debug_ipc::PlatformMessageLoop>();
  symbol_thread_ = std::make_unique<std::thread>([loop = symbol_loop_.get()]() {
    loop->Init();
    loop->Run();
    loop->Cleanup();
  });
}

SystemSymbolsProxy::~SystemSymbolsProxy() {
  // Delete SystemSymbols on the symbol thread and stop the message loop.
  symbol_loop_->PostTask(
      [loop = symbol_loop_.get(), symbols = symbols_.release()]() {
        delete symbols;
        loop->QuitNow();
      });
  symbol_thread_->join();
}

void SystemSymbolsProxy::Init(
    std::function<void(bool ids_loaded, const std::string& msg)> callback) {
  // Run SystemSymbols::Init on the background thread.
  symbol_loop_->PostTask(
      [symbols = symbols_.get(), main_loop = main_loop_, callback]() {
        std::string message;
        bool ids_loaded = symbols->Init(&message);

        // Post the output back to the main thread.
        main_loop->PostTask([ids_loaded, message, callback]() {
          callback(ids_loaded, message);
        });
      });
}

}  // namespace zxdb
