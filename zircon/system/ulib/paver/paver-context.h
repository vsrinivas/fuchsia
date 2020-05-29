#ifndef ZIRCON_SYSTEM_ULIB_PAVER_PAVER_CONTEXT_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_PAVER_CONTEXT_H_

#include <lib/sysconfig/sync-client.h>
#include <zircon/compiler.h>

#include <memory>
#include <mutex>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace paver {
// class ContextBase and class Context aim to provide a generic mechanism for
// updating and sharing board-specific context information.
// The context itself is hosted in the paver service but up to the board-specific
// device partitioners to interpret, initialize and update.
// Since there may be multiple clients at the same time, it is important to use
// the provided Context.lock when updating context to prevent data race.
class ContextBase {
 public:
  virtual ~ContextBase() = default;
};

// Following are device-specific context

class AstroPartitionerContext : public ContextBase {
 public:
  std::unique_ptr<::sysconfig::SyncClientBuffered> client_;

  AstroPartitionerContext(std::unique_ptr<::sysconfig::SyncClientBuffered> client)
      : client_{std::move(client)} {}
};

// The context wrapper
class Context {
 public:
  template <typename T>
  zx_status_t Initialize(std::function<zx_status_t(std::unique_ptr<T>*)> factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Already holds a context
    if (impl_) {
      return ZX_OK;
    }
    std::unique_ptr<T> out;
    if (auto status = factory(&out); status != ZX_OK) {
      return status;
    }
    impl_ = std::move(out);
    return ZX_OK;
  }

  // All functions using the contexts are callbacks so we can grab the
  // lock and do type checking ourselves internally.
  template <typename T>
  zx_status_t Call(std::function<zx_status_t(T*)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!impl_) {
      fprintf(stderr, "Context is not initialized.\n");
      return ZX_ERR_INTERNAL;
    }
    return callback(static_cast<T*>(impl_.get()));
  }

 private:
  std::mutex mutex_;
  std::unique_ptr<ContextBase> impl_ __TA_GUARDED(mutex_);
};

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_PAVER_CONTEXT_H_
