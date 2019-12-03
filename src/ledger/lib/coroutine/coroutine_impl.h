// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_COROUTINE_COROUTINE_IMPL_H_
#define SRC_LEDGER_LIB_COROUTINE_COROUTINE_IMPL_H_

#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "src/ledger/lib/coroutine/coroutine.h"

namespace context {
class Stack;
}

namespace coroutine {

class CoroutineServiceImpl : public CoroutineService {
 public:
  CoroutineServiceImpl();
  CoroutineServiceImpl(const CoroutineServiceImpl&) = delete;
  CoroutineServiceImpl& operator=(const CoroutineServiceImpl&) = delete;
  ~CoroutineServiceImpl() override;

  // CoroutineService.
  void StartCoroutine(fit::function<void(CoroutineHandler*)> runnable) override;

 private:
  class CoroutineHandlerImpl;

  std::vector<std::unique_ptr<context::Stack>> available_stack_;
  std::vector<std::unique_ptr<CoroutineHandlerImpl>> handlers_;
};

}  // namespace coroutine

#endif  // SRC_LEDGER_LIB_COROUTINE_COROUTINE_IMPL_H_
