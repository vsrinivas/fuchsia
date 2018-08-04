// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/icu_data/cpp/icu_data.h"

#include <lib/zx/vmar.h>

#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "third_party/icu/source/common/unicode/udata.h"

namespace icu_data {
namespace {

static constexpr char kIcuDataPath[] = "/pkg/data/icudtl.dat";
static uintptr_t g_icu_data_ptr = 0u;
static size_t g_icu_data_size = 0;

// Map the memory into the process and return a pointer to the memory.
// |size_out| is required and is set with the size of the mapped memory
// region.
uintptr_t GetData(const fsl::SizedVmo& icu_data, size_t* size_out) {
  if (!size_out)
    return 0u;
  uint64_t data_size = icu_data.size();
  if (data_size > std::numeric_limits<size_t>::max())
    return 0u;

  uintptr_t data = 0u;
  zx_status_t status = zx::vmar::root_self()->map(
      0, icu_data.vmo(), 0, static_cast<size_t>(data_size),
      ZX_VM_FLAG_PERM_READ, &data);
  if (status == ZX_OK) {
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
bool Initialize(component::StartupContext* context,
                const char* optional_data_path) {
  if (g_icu_data_ptr) {
    // Don't allow calling Initialize twice.
    return false;
  }

  const char* data_path = kIcuDataPath;
  if (optional_data_path != 0) {
    data_path = optional_data_path;
  }

  fsl::SizedVmo icu_data;
  if (!fsl::VmoFromFilename(data_path, &icu_data))
    return false;

  size_t data_size = 0;
  uintptr_t data = GetData(icu_data, &data_size);

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
    zx_status_t status =
        zx::vmar::root_self()->unmap(g_icu_data_ptr, g_icu_data_size);
    g_icu_data_ptr = 0u;
    g_icu_data_size = 0;
    return status == ZX_OK;
  } else {
    return false;
  }
}

}  // namespace icu_data
