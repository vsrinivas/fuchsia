// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/icu_data/cpp/icu_data.h"

#include <mx/vmar.h>

#include "lib/app/cpp/application_context.h"
#include "lib/icu_data/cpp/constants.h"
#include "lib/icu_data/fidl/icu_data.fidl.h"
#include "third_party/icu/source/common/unicode/udata.h"

namespace icu_data {
namespace {

static uintptr_t g_icu_data_ptr = 0u;
static size_t g_icu_data_size = 0;

// Helper function. Given a VMO handle, map the memory into the process and
// return a pointer to the memory.
// |size_out| is required and is set with the size of the mapped memory
// region.
uintptr_t GetDataFromVMO(const mx::vmo& vmo, size_t* size_out) {
  if (!size_out)
    return 0u;
  uint64_t data_size = 0u;
  mx_status_t status = vmo.get_size(&data_size);
  if (status != MX_OK || data_size > std::numeric_limits<size_t>::max())
    return 0u;

  uintptr_t data = 0u;
  status = mx::vmar::root_self().map(0, vmo, 0, static_cast<size_t>(data_size),
                                     MX_VM_FLAG_PERM_READ, &data);
  if (status == MX_OK) {
    *size_out = static_cast<size_t>(data_size);
    return data;
  }

  return 0u;
}

}  // namespace

// Initialize ICU data
//
// Connects to ICU Data Provider (a service) and requests the ICU data.
// Then, initializes ICU with the data received.
//
// Return value indicates if initialization was successful.
bool Initialize(app::ApplicationContext* context) {
  if (g_icu_data_ptr) {
    // Don't allow calling Initialize twice.
    return false;
  }

  // Get the data from the ICU data provider.
  icu_data::ICUDataProviderPtr icu_data_provider;
  context->ConnectToEnvironmentService(icu_data_provider.NewRequest());

  icu_data::ICUDataPtr response;
  icu_data_provider->ICUDataWithSha1(
      kDataHash,
      [&response](icu_data::ICUDataPtr r) { response = std::move(r); });
  icu_data_provider.WaitForIncomingResponse();

  if (!response) {
    return false;
  }

  size_t data_size = 0;
  uintptr_t data = GetDataFromVMO(response->vmo, &data_size);

  // Pass the data to ICU.
  if (data) {
    UErrorCode err = U_ZERO_ERROR;
    udata_setCommonData(reinterpret_cast<const char*>(data), &err);
    g_icu_data_ptr = data;
    g_icu_data_size = data_size;
    return err == U_ZERO_ERROR;
  } else {
    Release();
  }

  return false;
}

// Release mapped ICU data
//
// If Initialize() was called earlier, unmap the ICU data we had previously
// mapped to this process. ICU cannot be used after calling this method.
//
// Return value indicates if unmapping data was successful.
bool Release() {
  if (g_icu_data_ptr) {
    // Unmap the ICU data.
    mx_status_t status =
        mx::vmar::root_self().unmap(g_icu_data_ptr, g_icu_data_size);
    g_icu_data_ptr = 0u;
    g_icu_data_size = 0;
    return status == MX_OK;
  } else {
    return false;
  }
}

}  // namespace icu_data
