// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_utils.h"

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS  // squelch fopen()
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// Close a FILE descriptor, preserving errno to properly report previous
// I/O errors.
static void
file_close(FILE * f)
{
  int saved_errno = errno;
  (void)fclose(f);
  errno = saved_errno;
}

// Retrieve file size from a FILE decriptor.
// On success, return true and sets |*file_size|, false/errno otherwise.
// NOTE: This rewinds the descriptor to the start of the file on success!
static bool
file_get_size(FILE * const file, size_t * const file_size)
{
  if (fseek(file, 0, SEEK_END) != 0)
    return false;

  long size = ftell(file);
  if (size < 0)
    return false;

  if (fseek(file, 0, SEEK_SET) != 0)
    return false;

  *file_size = (size_t)size;
  return true;
}

// Read |file_size| bytes from |file| into heap-allocated memory.
// On success, return true and sets |*file_data|, false/errno otherwise.
static bool
file_read_data_from(FILE * file, size_t file_size, void ** file_data)
{
  *file_data = NULL;
  if (file_size == 0)
    return true;

  void * data = malloc(file_size);
  if (!data)
    return false;

  size_t bytes_read = fread(data, 1, file_size, file);
  if (bytes_read != file_size)
    {
      free(data);
      return false;
    }
  *file_data = data;
  return true;
}

bool
file_read(const char * const file_path, void ** const file_data, size_t * const file_size)
{
  *file_data = NULL;
  *file_size = 0;

  FILE * f = fopen(file_path, "rb");
  if (!f)
    return false;

  bool success = file_get_size(f, file_size) && file_read_data_from(f, *file_size, file_data);
  file_close(f);
  return success;
}

bool
file_write(const char * const file_path, const void * const file_data, size_t file_size)
{
  FILE * file = fopen(file_path, "wb");
  if (!file)
    return false;

  size_t bytes_written = fwrite(file_data, 1, file_size, file);
  file_close(file);
  return bytes_written == file_size;
}
