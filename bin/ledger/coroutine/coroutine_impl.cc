// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/coroutine/context/context.h"
#include "peridot/bin/ledger/coroutine/context/stack.h"

namespace coroutine {

constexpr size_t kMaxAvailableStacks = 25;

class CoroutineServiceImpl::CoroutineHandlerImpl : public CoroutineHandler {
 public:
  CoroutineHandlerImpl(std::unique_ptr<context::Stack> stack,
                       std::function<void(CoroutineHandler*)> runnable);
  ~CoroutineHandlerImpl() override;

  // CoroutineHandler.
  bool Yield() override;
  void Continue(bool interrupt) override;

  void Start();
  void set_cleanup(
      std::function<void(std::unique_ptr<context::Stack>)> cleanup) {
    cleanup_ = std::move(cleanup);
  }

 private:
  static void StaticRun(void* data);
  void Run();
  bool DoYield();

  std::unique_ptr<context::Stack> stack_;
  std::function<void(CoroutineHandler*)> runnable_;
  std::function<void(std::unique_ptr<context::Stack>)> cleanup_;
  context::Context main_context_;
  context::Context routine_context_;
  bool interrupted_ = false;
  bool finished_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(CoroutineHandlerImpl);
};

CoroutineServiceImpl::CoroutineHandlerImpl::CoroutineHandlerImpl(
    std::unique_ptr<context::Stack> stack,
    std::function<void(CoroutineHandler*)> runnable)
    : stack_(std::move(stack)), runnable_(std::move(runnable)) {
  FXL_DCHECK(stack_);
  FXL_DCHECK(runnable_);
}

CoroutineServiceImpl::CoroutineHandlerImpl::~CoroutineHandlerImpl() {
  FXL_DCHECK(!stack_);
}

bool CoroutineServiceImpl::CoroutineHandlerImpl::Yield() {
  FXL_DCHECK(!interrupted_);

  if (interrupted_) {
    return true;
  }

  return DoYield();
}

void CoroutineServiceImpl::CoroutineHandlerImpl::Continue(bool interrupt) {
  FXL_DCHECK(!finished_);

  interrupted_ = interrupted_ || interrupt;
  context::SwapContext(&main_context_, &routine_context_);

  if (finished_) {
    cleanup_(std::move(stack_));
    // this object has been deleted by |cleanup_|, return.
    return;
  }
}

void CoroutineServiceImpl::CoroutineHandlerImpl::Start() {
  context::MakeContext(&routine_context_, stack_.get(),
                       &CoroutineServiceImpl::CoroutineHandlerImpl::StaticRun,
                       this);
  Continue(false);
}

void CoroutineServiceImpl::CoroutineHandlerImpl::StaticRun(void* data) {
  reinterpret_cast<CoroutineHandlerImpl*>(data)->Run();
}

void CoroutineServiceImpl::CoroutineHandlerImpl::Run() {
  runnable_(this);
  // Delete |runnable_|, as it can have side effects that should be run inside
  // the co-routine.
  runnable_ = [](CoroutineHandler*) {};
  finished_ = true;
  DoYield();
  FXL_NOTREACHED() << "Last yield should never return.";
}

bool CoroutineServiceImpl::CoroutineHandlerImpl::DoYield() {
  context::SwapContext(&routine_context_, &main_context_);
  return interrupted_;
}

CoroutineServiceImpl::CoroutineServiceImpl() {}

CoroutineServiceImpl::~CoroutineServiceImpl() {
  while (!handlers_.empty()) {
    handlers_.back()->Continue(true);
  }
}

void CoroutineServiceImpl::StartCoroutine(
    std::function<void(CoroutineHandler* handler)> runnable) {
  std::unique_ptr<context::Stack> stack;
  if (available_stack_.empty()) {
    stack = std::make_unique<context::Stack>();
  } else {
    stack = std::move(available_stack_.back());
    available_stack_.pop_back();
  }
  auto handler = std::make_unique<CoroutineHandlerImpl>(std::move(stack),
                                                        std::move(runnable));
  auto handler_ptr = handler.get();
  handler->set_cleanup([this,
                        handler_ptr](std::unique_ptr<context::Stack> stack) {
    if (available_stack_.size() < kMaxAvailableStacks) {
      stack->Release();
      available_stack_.push_back(std::move(stack));
    }
    handlers_.erase(std::find_if(
        handlers_.begin(), handlers_.end(),
        [handler_ptr](const std::unique_ptr<CoroutineHandlerImpl>& handler) {
          return handler.get() == handler_ptr;
        }));
  });
  handlers_.push_back(std::move(handler));
  handler_ptr->Start();
}

}  // namespace coroutine
