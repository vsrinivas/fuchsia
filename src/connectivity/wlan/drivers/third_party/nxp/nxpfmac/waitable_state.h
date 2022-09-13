// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WAITABLE_STATE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WAITABLE_STATE_H_

#include <condition_variable>
#include <mutex>

namespace wlan::nxpfmac {

// Wrapper around a state and a condition variable to ensure that the correct operations are always
// taken. Note that Store and Load (and the equivalent operator overloads) do not perform any
// locking. Access to this class should be protected by an external mutex, the same mutex should be
// used to create the lock passed to Wait.
template <typename T>
class WaitableState {
 public:
  explicit WaitableState(const T& initial_state) : state_(initial_state) {}
  WaitableState(const WaitableState&) = delete;
  WaitableState& operator=(const WaitableState&) = delete;

  void Store(const T& state) {
    state_ = state;
    condition_.notify_all();
  }
  WaitableState& operator=(const T& state) {
    Store(state);
    return *this;
  }

  const T& Load() const { return state_; }
  explicit operator T() const { return state_; }

  // This function behaves similar to std::condition_variable. Pass it a lock that is currently
  // held by the calling thread. While the state is not equal to `expected_state` the lock is not
  // held, allowing another thread to modify the state.
  void Wait(std::unique_lock<std::mutex>& lock, const T& expected_state) {
    while (state_ != expected_state) {
      condition_.wait(lock);
    }
  }

 private:
  T state_;
  std::condition_variable condition_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WAITABLE_STATE_H_
