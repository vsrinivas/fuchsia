#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu-manager.h"

#include <lib/async/cpp/task.h>
#include <lib/stdcompat/atomic.h>
#include <zircon/assert.h>

#include <limits>

namespace wlan::iwlwifi {

// static
thread_local int RcuManager::read_lock_count_ = 0;

RcuManager::RcuManager(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

RcuManager::~RcuManager() {
  zx_status_t status = ZX_OK;

  // Wait for all existing calls to complete.
  cpp20::atomic_ref<zx_futex_t> call_count_ref(call_count_);
  zx_futex_t count = call_count_ref.load(std::memory_order_acquire);
  while (count > 0) {
    if ((status = zx_futex_wait(&call_count_, count, ZX_HANDLE_INVALID, ZX_TIME_INFINITE)) !=
        ZX_OK) {
      if (status != ZX_ERR_BAD_STATE) {
        break;
      }
    }
    count = call_count_ref.load(std::memory_order_acquire);
  }
}

void RcuManager::InitForThread() { read_lock_count_ = 0; }

void RcuManager::ReadLock() {
  if (++read_lock_count_ == 1) {
    rwlock_.lock_shared();
  }
}

void RcuManager::ReadUnlock() {
  ZX_DEBUG_ASSERT(read_lock_count_ > 0);
  if (--read_lock_count_ == 0) {
    rwlock_.unlock_shared();
  }
}

void RcuManager::Sync() {
  // Sync only has to ensure that there are no more outstanding reader locks.
  rwlock_.lock();
  rwlock_.unlock();
}

void RcuManager::CallSync(void (*func)(void*), void* data) {
  // Post the task to the worker dispatcher.  This has the advantages of:
  // * Not immediately blocking the current thread.
  // * The worker dispatcher is often another thread that uses RCUs.  By posting the task to this
  //   thread, we ensure that it cannot also be locking for RCU at the same time, thus reducing
  //   contention.
  cpp20::atomic_ref<zx_futex_t> call_count_ref(call_count_);
  call_count_ref.fetch_add(1, std::memory_order_release);
  ::async::PostTask(dispatcher_, [this, func, data]() {
    Sync();
    func(data);

    // Signal waiters that may be waiting for all calls to complete.
    cpp20::atomic_ref<zx_futex_t> call_count_ref(call_count_);
    if (call_count_ref.fetch_sub(1, std::memory_order_release) == 1) {
      zx_futex_wake(&call_count_, std::numeric_limits<uint32_t>::max());
    }
  });
}

void RcuManager::FreeSync(void* alloc) { CallSync(&free, alloc); }

}  // namespace wlan::iwlwifi
