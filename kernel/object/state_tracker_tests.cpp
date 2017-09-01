// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/state_tracker.h>

#include <object/state_observer.h>
#include <unittest.h>

// Tests for observer removal
namespace removal {

class RemovableObserver : public StateObserver {
public:
    RemovableObserver() = default;

    // The number of times OnRemoved() has been called.
    int removals() const { return removals_; }

private:
    // No-op overrides of pure virtuals.
    Flags OnInitialize(mx_signals_t initial_state,
                       const StateObserver::CountInfo* cinfo) override {
        return 0;
    }
    Flags OnStateChange(mx_signals_t new_state) override { return 0; }
    Flags OnCancel(Handle* handle) override { return 0; }
    Flags OnCancelByKey(Handle* handle, const void* port, uint64_t key)
        override { return 0; }

    void OnRemoved() override { removals_++; }

    int removals_ = 0;
};

// Helper: Causes most On*() hooks (except for OnInitialized) to be called on
// all of |st|'s observers.
void call_all_on_hooks(StateTracker* st) {
    st->UpdateState(0, 7);
    uint32_t count = 5;
    st->UpdateLastHandleSignal(&count);
    count = 1;
    st->UpdateLastHandleSignal(&count);
    st->Cancel(/* handle= */ nullptr);
    st->CancelByKey(/* handle= */ nullptr, /* port= */ nullptr, /* key= */ 2u);
}

bool on_initialize(void* context) {
    BEGIN_TEST;

    class RmOnInitialize : public RemovableObserver {
    public:
        Flags OnInitialize(mx_signals_t initial_state,
                           const StateObserver::CountInfo* cinfo) override {
            return kNeedRemoval;
        }
    };

    RmOnInitialize obs;
    EXPECT_EQ(0, obs.removals(), "");

    // Cause OnInitialize() to be called.
    StateTracker st;
    st.AddObserver(&obs, nullptr);

    // Should have been removed.
    EXPECT_EQ(1, obs.removals(), "");

    // Further On hook calls should not re-remove.
    call_all_on_hooks(&st);
    EXPECT_EQ(1, obs.removals(), "");

    END_TEST;
}

class RmOnStateChange : public RemovableObserver {
public:
    Flags OnStateChange(mx_signals_t new_state) override {
        return kNeedRemoval;
    }
};

bool on_state_change_via_update_state(void* context) {
    BEGIN_TEST;

    RmOnStateChange obs;
    EXPECT_EQ(0, obs.removals(), "");

    StateTracker st;
    st.AddObserver(&obs, nullptr);
    EXPECT_EQ(0, obs.removals(), ""); // Not removed yet.

    // Cause OnStateChange() to be called.
    st.UpdateState(0, 1);

    // Should have been removed.
    EXPECT_EQ(1, obs.removals(), "");

    // Further On hook calls should not re-remove.
    call_all_on_hooks(&st);
    EXPECT_EQ(1, obs.removals(), "");

    END_TEST;
}

bool on_state_change_via_last_handle(void* context) {
    BEGIN_TEST;

    RmOnStateChange obs;
    EXPECT_EQ(0, obs.removals(), "");

    StateTracker st;
    st.AddObserver(&obs, nullptr);
    EXPECT_EQ(0, obs.removals(), ""); // Not removed yet.

    // Cause OnStateChange() to be called. Need to transition out of and
    // back into MX_SIGNAL_LAST_HANDLE, because it's asserted by default.
    uint32_t count = 2;
    st.UpdateLastHandleSignal(&count);
    count = 1;
    st.UpdateLastHandleSignal(&count);

    // Should have been removed.
    EXPECT_EQ(1, obs.removals(), "");

    // Further On hook calls should not re-remove.
    call_all_on_hooks(&st);
    EXPECT_EQ(1, obs.removals(), "");

    END_TEST;
}

bool on_cancel(void* context) {
    BEGIN_TEST;

    class RmOnCancel : public RemovableObserver {
    public:
        Flags OnCancel(Handle* handle) {
            return kNeedRemoval;
        }
    };

    RmOnCancel obs;
    EXPECT_EQ(0, obs.removals(), "");

    StateTracker st;
    st.AddObserver(&obs, nullptr);
    EXPECT_EQ(0, obs.removals(), ""); // Not removed yet.

    // Cause OnCancel() to be called.
    st.Cancel(/* handle= */ nullptr);

    // Should have been removed.
    EXPECT_EQ(1, obs.removals(), "");

    // Further On hook calls should not re-remove.
    call_all_on_hooks(&st);
    EXPECT_EQ(1, obs.removals(), "");

    END_TEST;
}

bool on_cancel_by_key(void* context) {
    BEGIN_TEST;

    class RmOnCancelByKey : public RemovableObserver {
    public:
        Flags OnCancelByKey(Handle* handle, const void* port, uint64_t key)
            override {
            return kNeedRemoval;
        }
    };

    RmOnCancelByKey obs;
    EXPECT_EQ(0, obs.removals(), "");

    StateTracker st;
    st.AddObserver(&obs, nullptr);
    EXPECT_EQ(0, obs.removals(), ""); // Not removed yet.

    // Cause OnCancelByKey() to be called.
    st.CancelByKey(/* handle= */ nullptr, /* port= */ nullptr, /* key= */ 2u);

    // Should have been removed.
    EXPECT_EQ(1, obs.removals(), "");

    // Further On hook calls should not re-remove.
    call_all_on_hooks(&st);
    EXPECT_EQ(1, obs.removals(), "");

    END_TEST;
}

} // namespace removal

#define ST_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(state_tracker_tests)

ST_UNITTEST(removal::on_initialize)
ST_UNITTEST(removal::on_state_change_via_update_state)
ST_UNITTEST(removal::on_state_change_via_last_handle)
ST_UNITTEST(removal::on_cancel)
ST_UNITTEST(removal::on_cancel_by_key)

UNITTEST_END_TESTCASE(
    state_tracker_tests, "statetracker", "StateTracker test", nullptr, nullptr);
