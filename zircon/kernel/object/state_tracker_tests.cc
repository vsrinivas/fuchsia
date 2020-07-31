// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/signal_observer.h>

namespace {
class TestDispatcher;
}

template <>
struct CanaryTag<TestDispatcher> {
  static constexpr uint32_t magic = 0;
};

namespace {

class TestDispatcher final : public SoloDispatcher<TestDispatcher, ZX_RIGHTS_BASIC> {
 public:
  TestDispatcher() {}
  ~TestDispatcher() final = default;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_NONE; }

  void SetSignals(zx_signals_t signals) {
    this->UpdateState(/*clear_mask=*/0, /*set_mask=*/signals);
  }
};

class TestSignalObserver final : public SignalObserver {
 public:
  TestSignalObserver() = default;
  TestSignalObserver(void* port, uint64_t key) : port_(port), key_(key) {}

  zx_signals_t signals() { return signals_; }

  bool cancel_called() { return cancel_called_; }
  bool match_called() { return match_called_; }
  bool called() { return match_called_ || cancel_called_; }

 private:
  void OnMatch(zx_signals_t signals) final {
    // Ensure we are not called twice.
    ZX_ASSERT(!cancel_called_);
    ZX_ASSERT(!match_called_);

    signals_ = signals;
    match_called_ = true;
  }

  void OnCancel(zx_signals_t signals) final {
    // Ensure we are not called twice.
    ZX_ASSERT(!cancel_called_);
    ZX_ASSERT(!match_called_);

    signals_ = signals;
    cancel_called_ = true;
  }

  bool MatchesKey(const void* port, uint64_t key) final { return port == port_ && key == key_; }

  zx_signals_t signals_ = 0;
  bool cancel_called_ = false;
  bool match_called_ = false;

  void* port_;
  uint64_t key_;
};

bool TestBasicMatch() {
  BEGIN_TEST;

  TestSignalObserver observer;
  TestDispatcher dispatcher;

  // Add the observer.
  ASSERT_EQ(ZX_OK, dispatcher.AddObserver(&observer, nullptr, ZX_USER_SIGNAL_0));
  ASSERT_FALSE(observer.called());

  // Set an unrelated signal.
  dispatcher.SetSignals(ZX_USER_SIGNAL_1);
  ASSERT_FALSE(observer.called());

  // Set the triggered signal.
  dispatcher.SetSignals(ZX_USER_SIGNAL_0);
  ASSERT_TRUE(observer.called());

  END_TEST;
}

bool TestAlreadyMatched() {
  BEGIN_TEST;

  TestDispatcher dispatcher;
  dispatcher.SetSignals(ZX_USER_SIGNAL_0);

  TestSignalObserver observer;

  // Add the observer, when the signal has already matched.
  ASSERT_EQ(ZX_OK, dispatcher.AddObserver(&observer, nullptr, ZX_USER_SIGNAL_0));
  ASSERT_TRUE(observer.match_called());

  END_TEST;
}

bool TestCancelled() {
  BEGIN_TEST;

  TestSignalObserver observer;

  // Create a dispatcher and some handles.
  fbl::AllocChecker ac;
  auto dispatcher = fbl::MakeRefCountedChecked<TestDispatcher>(&ac);
  ASSERT_TRUE(ac.check());

  HandleOwner handle1 = Handle::Make(dispatcher, TestDispatcher::default_rights());
  HandleOwner handle2 = Handle::Make(dispatcher, TestDispatcher::default_rights());

  // Add the observer.
  ASSERT_EQ(ZX_OK, dispatcher->AddObserver(&observer, handle1.get(), ZX_USER_SIGNAL_0));
  ASSERT_FALSE(observer.called());

  // Cancel an unrelated handle.
  dispatcher->Cancel(handle2.get());
  ASSERT_FALSE(observer.called());

  // Cancel the associated handle.
  dispatcher->Cancel(handle1.get());
  ASSERT_TRUE(observer.cancel_called());

  END_TEST;
}

bool TestRemoveObserver() {
  BEGIN_TEST;

  TestSignalObserver observer;
  TestDispatcher dispatcher;

  // Add the observer.
  ASSERT_EQ(ZX_OK, dispatcher.AddObserver(&observer, nullptr, ZX_USER_SIGNAL_0));

  // Remove it again.
  EXPECT_TRUE(dispatcher.RemoveObserver(&observer));

  // Remove it yet again, but expect "false" return code.
  EXPECT_FALSE(dispatcher.RemoveObserver(&observer));

  // Trigger the signal: it shouldn't fire.
  dispatcher.SetSignals(ZX_USER_SIGNAL_0);
  ASSERT_FALSE(observer.called());

  END_TEST;
}

bool TestRemoveObserverAfterMatch() {
  BEGIN_TEST;

  TestSignalObserver observer;
  TestDispatcher dispatcher;

  // Add the observer.
  ASSERT_EQ(ZX_OK, dispatcher.AddObserver(&observer, nullptr, ZX_USER_SIGNAL_0));

  // Fire the signal.
  dispatcher.SetSignals(ZX_USER_SIGNAL_0);
  EXPECT_TRUE(observer.match_called());

  // Removing the observer after a match should return false.
  EXPECT_FALSE(dispatcher.RemoveObserver(&observer));

  END_TEST;
}

bool TestRemoveByKey() {
  BEGIN_TEST;

  // Create a dispatcher and some handles.
  fbl::AllocChecker ac;
  auto dispatcher = fbl::MakeRefCountedChecked<TestDispatcher>(&ac);
  ASSERT_TRUE(ac.check());

  HandleOwner handle1 = Handle::Make(dispatcher, TestDispatcher::default_rights());
  HandleOwner handle2 = Handle::Make(dispatcher, TestDispatcher::default_rights());

  // Create an observer with the given port and key.
  int dummy_port;
  const uint64_t dummy_key = 0x123;
  TestSignalObserver observer{&dummy_port, dummy_key};

  // Add the observer.
  ASSERT_EQ(ZX_OK, dispatcher->AddObserver(&observer, handle1.get(), ZX_USER_SIGNAL_0));

  // Cancel the wrong handle / port / key.
  int different_port;
  ASSERT_FALSE(dispatcher->CancelByKey(handle2.get(), &dummy_port, dummy_key));
  ASSERT_FALSE(dispatcher->CancelByKey(handle1.get(), &different_port, dummy_key));
  ASSERT_FALSE(dispatcher->CancelByKey(handle1.get(), &dummy_port, 0x321));
  ASSERT_FALSE(observer.called());

  // Cancel the correct handle / port / key combination.
  ASSERT_TRUE(dispatcher->CancelByKey(handle1.get(), &dummy_port, dummy_key));
  ASSERT_TRUE(observer.cancel_called());

  END_TEST;
}

}  // namespace

#define ST_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(state_tracker_tests)

ST_UNITTEST(TestBasicMatch)
ST_UNITTEST(TestAlreadyMatched)
ST_UNITTEST(TestCancelled)
ST_UNITTEST(TestRemoveObserver)
ST_UNITTEST(TestRemoveObserverAfterMatch)
ST_UNITTEST(TestRemoveByKey)

UNITTEST_END_TESTCASE(state_tracker_tests, "statetracker", "StateTracker test")
