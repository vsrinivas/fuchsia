// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/icu_data/cpp/icu_data.h"

#include <lib/zx/vmar.h>
#include <src/lib/files/directory.h>
#include <zircon/errors.h>

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
      0, icu_data.vmo(), 0, static_cast<size_t>(data_size), ZX_VM_PERM_READ, &data);
  if (status == ZX_OK) {
    *size_out = static_cast<size_t>(data_size);
    return data;
  }

  return 0u;
}

}  // namespace

zx_status_t Initialize() { return InitializeWithTzResourceDir(nullptr); }

zx_status_t InitializeWithTzResourceDir(const char tz_files_dir[]) {
  if (g_icu_data_ptr) {
    // Don't allow calling Initialize twice.
    return ZX_ERR_ALREADY_BOUND;
  }

  if (tz_files_dir && !files::IsDirectory(tz_files_dir)) {
    return ZX_ERR_NOT_DIR;
  }

  if (tz_files_dir) {
    // This is how we configure ICU to load time zone resource files from a
    // separate directory. See
    // http://userguide.icu-project.org/datetime/timezone#TOC-ICU4C-TZ-Update-with-Drop-in-.res-files-ICU-54-and-newer-
    setenv("ICU_tz_files_dir", tz_files_dir,
           /* overwrite existing env variable */ 1);
  }

  fsl::SizedVmo icu_data;
  if (!fsl::VmoFromFilename(kIcuDataPath, &icu_data))
    return ZX_ERR_IO;

  size_t data_size = 0;
  uintptr_t data = GetData(icu_data, &data_size);

  // Pass the data to ICU.
  if (data) {
    UErrorCode err = U_ZERO_ERROR;
    udata_setCommonData(reinterpret_cast<const char*>(data), &err);
    g_icu_data_ptr = data;
    g_icu_data_size = data_size;
    return (err == U_ZERO_ERROR) ? ZX_OK : ZX_ERR_INTERNAL;
  } else {
    Release();
    return ZX_ERR_INTERNAL;
  }
}

zx_status_t Release() {
  if (g_icu_data_ptr) {
    // Unmap the ICU data.
    zx_status_t status = zx::vmar::root_self()->unmap(g_icu_data_ptr, g_icu_data_size);
    g_icu_data_ptr = 0u;
    g_icu_data_size = 0;
    return status;
  } else {
    return ZX_ERR_BAD_STATE;
  }
}

}  // namespace icu_data
