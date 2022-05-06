// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_

// This should point to the abr.h in the abr library in firmware sdk.
#include <lib/abr/abr.h>
// This should point to the zbi.h in the zbi library in firmware sdk..
#include <lib/zbi/zbi.h>

__BEGIN_CDECLS

// Appends a cmdline ZBI item containing the current slot information to a ZBI container.
// For example, "zvb.current_slot=_a"
//
// Returns ZBI_RESULT_OK on success, Error code otherwise.
zbi_result_t AppendCurrentSlotZbiItem(zbi_header_t* zbi, size_t capacity, AbrSlotIndex slot);

// Appends a file to a ZBI container. The function creates a new entry of ZBI_TYPE_BOOTLOADER_FILE
// type in the container, containing file name and content. The file will be available by the
// filesystem service in bootsvc.
//
// @zbi: Pointer to the container.
// @capacity: Size of the container.
// @name: Name of the file.
// @file_data: Content of the file.
// @file_data_size: Size of the content.
//
// Returns ZBI_RESULT_OK on success. Error code otherwise.
zbi_result_t AppendZbiFile(zbi_header_t* zbi, size_t capacity, const char* name,
                           const void* file_data, size_t file_data_size);

// A callback function to read a factory file given the name.
//
// @context: Caller data for the function to be called with.
// @name: name of the file
// @capacity: Maximum size of the output buffer.
// @output: Pointer to the output buffer
// @out_len: Output pointer that stores the size of the data written.
//
// Returns true on success, false on failures.
typedef bool (*read_factory_t)(void* context, const char* name, size_t capacity, void* output,
                               size_t* out_len);

// Appends a list of file to a ZBI container as a factory bootfs item. The API will try to read as
// many files as possible. If a file fails to be read and added, it will be skipped.
//
// @zbi: Pointer to the container.
// @capacity: Size of the container.
// @file_names: Names of the files.
// @file_count: Size of `file_names`
// @read_factory: A function pointer that returns the payload according to file name in `file_names`
// @read_factory_context: Caller data that `read_factory` will be called with.
//
// Returns ZBI_RESULT_OK on success. Error code otherwise.
zbi_result_t AppendBootfsFactoryFiles(zbi_header_t* zbi, size_t capacity, const char** file_names,
                                      size_t file_count, read_factory_t read_factory,
                                      void* read_factory_context);
__END_CDECLS

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZBI_UTILS_H_
