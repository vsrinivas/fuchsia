// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform/efi.h>
#include <platform/efi_crashlog.h>

namespace {
efi_guid kZirconVendorGuid = ZIRCON_VENDOR_GUID;
char16_t kZirconCrashlogEfiVarName[] = ZIRCON_CRASHLOG_EFIVAR;
}  // namespace

NO_ASAN void EfiCrashlog::Finalize(zircon_crash_reason_t reason, size_t amt) {
  // Switch into the EFI address space.
  EfiServicesActivation services = TryActivateEfiServices();
  if (!services.valid()) {
    return;
  }

  // Store the log.
  amt = ktl::min(amt, sizeof(render_target_));
  efi_status result = services->SetVariable(kZirconCrashlogEfiVarName, &kZirconVendorGuid,
                                            ZIRCON_CRASHLOG_EFIATTR, amt, render_target_);

  // If we are writing a zero length crashlog then this has the meaning of deleting the variable
  // from the efi storage, if the crashlog already doesn't exist then attempting to delete it is
  // results in an error. From our point of view this error is spurious and so we avoid printing a
  // confusing error message.
  if (result != EFI_SUCCESS && (result != EFI_NOT_FOUND || amt > 0)) {
    printf("EFI error while attempting to store crashlog: %" PRIx64 "\n", result);
    return;
  }
}

size_t EfiCrashlog::Recover(FILE* tgt) {
  ktl::string_view last_crashlog;
  {
    Guard<SpinLock, IrqSave> guard{&last_crashlog_lock_};
    last_crashlog = last_crashlog_;
  }

  if (last_crashlog.empty()) {
    return 0;
  }

  // If the user actually supplied a target, copy the crashlog into it.
  // Otherwise, just return the length which would have been needed to hold the
  // entire log.
  if (tgt != nullptr) {
    return ktl::max(tgt->Write(last_crashlog), 0);
  }
  return last_crashlog.size();
}
