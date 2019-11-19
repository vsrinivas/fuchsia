// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <ktl/move.h>
#include <object/dispatcher.h>
#include <object/event_pair_dispatcher.h>

#include "object/handle.h"

namespace {

// A Dispatcher-like class that tracks the number of calls to on_zero_handles()
// for testing purposes.
//
// This base class is available so that we can test that KernelHandle can
// properly upcast child->base RefPtrs.
class FakeDispatcherBase : public fbl::RefCounted<FakeDispatcherBase> {
 public:
  virtual ~FakeDispatcherBase() = default;

  int on_zero_handles_calls() const { return on_zero_handles_calls_; }
  void on_zero_handles() { on_zero_handles_calls_++; }

 protected:
  FakeDispatcherBase() = default;

 private:
  int on_zero_handles_calls_ = 0;
};

class FakeDispatcher : public FakeDispatcherBase {
 public:
  static fbl::RefPtr<FakeDispatcher> Create() {
    fbl::AllocChecker ac;
    auto dispatcher = fbl::AdoptRef(new (&ac) FakeDispatcher());
    if (!ac.check()) {
      unittest_printf("Failed to allocate FakeDispatcher\n");
      return nullptr;
    }
    return dispatcher;
  }

  ~FakeDispatcher() override = default;

 private:
  FakeDispatcher() = default;
};

bool KernelHandleCreate() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  {
    KernelHandle handle(dispatcher);
    EXPECT_EQ(dispatcher.get(), handle.dispatcher().get());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleCreateUpcast() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  {
    KernelHandle<FakeDispatcherBase> handle(dispatcher);
    EXPECT_EQ(dispatcher.get(), handle.dispatcher().get());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleReset() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  fbl::RefPtr<FakeDispatcher> dispatcher2 = FakeDispatcher::Create();
  {
    KernelHandle handle(dispatcher);

    handle.reset(dispatcher2);
    EXPECT_EQ(dispatcher2.get(), handle.dispatcher().get());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);
    EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleResetUpcast() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  fbl::RefPtr<FakeDispatcher> dispatcher2 = FakeDispatcher::Create();
  {
    KernelHandle<FakeDispatcherBase> handle(dispatcher);

    handle.reset(dispatcher2);
    EXPECT_EQ(dispatcher2.get(), handle.dispatcher().get());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);
    EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleResetToNull() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  KernelHandle handle(dispatcher);

  handle.reset();
  EXPECT_NULL(handle.dispatcher());
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleRelease() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  KernelHandle handle(dispatcher);

  fbl::RefPtr<FakeDispatcher> dispatcher_copy = handle.release();
  EXPECT_NULL(handle.dispatcher());
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 0);
  EXPECT_EQ(dispatcher.get(), dispatcher_copy.get());

  END_TEST;
}

bool KernelHandleMoveConstructor() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  KernelHandle handle(dispatcher);
  {
    KernelHandle new_handle(ktl::move(handle));
    EXPECT_NULL(handle.dispatcher());
    EXPECT_NONNULL(new_handle.dispatcher());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleMoveConstructorUpcast() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  KernelHandle handle(dispatcher);
  {
    KernelHandle<FakeDispatcherBase> new_handle(ktl::move(handle));
    EXPECT_NULL(handle.dispatcher());
    EXPECT_NONNULL(new_handle.dispatcher());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleMoveAssignment() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  fbl::RefPtr<FakeDispatcher> dispatcher2 = FakeDispatcher::Create();
  {
    KernelHandle handle(dispatcher);
    KernelHandle handle2(dispatcher2);

    handle = ktl::move(handle2);
    EXPECT_NONNULL(handle.dispatcher());
    EXPECT_NULL(handle2.dispatcher());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);
    EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleMoveAssignmentUpcast() {
  BEGIN_TEST;

  fbl::RefPtr<FakeDispatcher> dispatcher = FakeDispatcher::Create();
  fbl::RefPtr<FakeDispatcher> dispatcher2 = FakeDispatcher::Create();
  {
    KernelHandle<FakeDispatcherBase> handle(dispatcher);
    KernelHandle handle2(dispatcher2);

    handle = ktl::move(handle2);
    EXPECT_NONNULL(handle.dispatcher());
    EXPECT_NULL(handle2.dispatcher());
    EXPECT_EQ(dispatcher->on_zero_handles_calls(), 1);
    EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 0);
  }
  EXPECT_EQ(dispatcher2->on_zero_handles_calls(), 1);

  END_TEST;
}

bool KernelHandleUpgrade() {
  BEGIN_TEST;

  // HandleOwner requires a real Dispatcher so we can't use FakeDispatcher
  // here. Use eventpair instead since we can signal the peer to check
  // whether its on_zero_handles() has been called.
  KernelHandle<EventPairDispatcher> eventpair[2];
  zx_rights_t rights;
  ASSERT_EQ(EventPairDispatcher::Create(&eventpair[0], &eventpair[1], &rights), ZX_OK);
  {
    HandleOwner handle_owner;
    {
      handle_owner = Handle::Make(ktl::move(eventpair[0]), rights);
      EXPECT_NULL(eventpair[0].dispatcher());
      EXPECT_TRUE(handle_owner);
      EXPECT_EQ(handle_owner->rights(), rights);
    }
    EXPECT_EQ(eventpair[1].dispatcher()->user_signal_peer(0, ZX_USER_SIGNAL_0), ZX_OK);
  }
  EXPECT_EQ(eventpair[1].dispatcher()->user_signal_peer(0, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(handle_tests)
UNITTEST("KernelHandleCreate", KernelHandleCreate)
UNITTEST("KernelHandleCreateUpcast", KernelHandleCreateUpcast)
UNITTEST("KernelHandleReset", KernelHandleReset)
UNITTEST("KernelHandleResetUpcast", KernelHandleResetUpcast)
UNITTEST("KernelHandleResetToNull", KernelHandleResetToNull)
UNITTEST("KernelHandleRelease", KernelHandleRelease)
UNITTEST("KernelHandleMoveConstructor", KernelHandleMoveConstructor)
UNITTEST("KernelHandleMoveConstructorUpcast", KernelHandleMoveConstructorUpcast)
UNITTEST("KernelHandleMoveAssignment", KernelHandleMoveAssignment)
UNITTEST("KernelHandleMoveAssignmentUpcast", KernelHandleMoveAssignmentUpcast)
UNITTEST("KernelHandleUpgrade", KernelHandleUpgrade)
UNITTEST_END_TESTCASE(handle_tests, "handle", "Handle test")
