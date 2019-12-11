#include "src/ledger/bin/synchronization/dispatcher_checker.h"

#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace ledger {

bool DispatcherChecker::IsCreationDispatcherCurrent() const {
  if (async_get_default_dispatcher() == self_) {
    return true;
  }
  // If ASAN is enabled, log stack of creation of both dispatchers.
#if __has_feature(address_sanitizer)
  __asan_describe_address(self_);
  __asan_describe_address(async_get_default_dispatcher());
#endif
  return false;
}

}  // namespace ledger
