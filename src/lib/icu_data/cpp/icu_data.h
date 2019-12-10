// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ICU_DATA_CPP_ICU_DATA_H_
#define SRC_LIB_ICU_DATA_CPP_ICU_DATA_H_

#include <zircon/types.h>

namespace icu_data {

// Initialize ICU data
//
// Connects to ICU Data Provider (a service) and requests the ICU data.
// Then, initializes ICU with the data received.
//
// Return value indicates if initialization was successful (ZX_OK).
// If ICU Data has already been initialized, returns ZX_ERR_ALREADY_BOUND.
zx_status_t Initialize();

// Initialize ICU data, but use separate resource files for time zone data.
//
// Loads time zone resource files from .res files in the specified directory.
// If the files do not exist, ICU will fall back to using the main data file
// for time zone data; there is no way to detect this edge case.
//
// For details on loading time zone resource files, see
// http://userguide.icu-project.org/datetime/timezone#TOC-ICU4C-TZ-Update-with-Drop-in-.res-files-ICU-54-and-newer-
//
// Return value indicates if initialization was successful (ZX_OK).
// If ICU Data has already been initialized, returns ZX_ERR_ALREADY_BOUND.
zx_status_t InitializeWithTzResourceDir(const char tz_files_dir[]);

// Initialize ICU data, but use separate resource files for time zone data.
//
// Loads time zone resource files from .res files in the specified directory.
// Also reads an expected time zone database revision ID, e.g. "2019c", from the
// file at revision_file_path and verifies that the loaded data matches.
// If the revision file cannot be read, or if the loaded ICU data contains a
// different time zone data revision, returns an error.
//
// If the .res files do not exist, ICU will fall back to using the main data
// file for time zone data; there is no way to detect this edge case (unless
// there is also a revision mismatch).
//
// For details on loading time zone resource files, see
// http://userguide.icu-project.org/datetime/timezone#TOC-ICU4C-TZ-Update-with-Drop-in-.res-files-ICU-54-and-newer-
//
// Return value indicates if initialization was successful (ZX_OK).
// If ICU Data has already been initialized, returns ZX_ERR_ALREADY_BOUND.
// If the time zone data has the wrong revision, returns
// ZX_ERR_IO_DATA_INTEGRITY.
zx_status_t InitializeWithTzResourceDirAndValidate(const char tz_files_dir[],
                                                   const char tz_revision_file_path[]);

// Release mapped ICU data
//
// Ifna Initialize() was called earlier, unmap the ICU data we had previously
// mapped to this process. ICU cannot be used after calling this method.
//
// Return value indicates if unmapping data was successful (ZX_OK).
// If ICU data was never loaded, returns ZX_ERR_BAD_STATE.
zx_status_t Release();

}  // namespace icu_data

#endif  // SRC_LIB_ICU_DATA_CPP_ICU_DATA_H_
