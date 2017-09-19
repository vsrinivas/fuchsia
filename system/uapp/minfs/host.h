// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PATH_PREFIX "::"
#define PREFIX_SIZE 2

int emu_open(const char* path, int flags, mode_t mode);
int emu_close(int fd);
int emu_mkdir(const char* path, mode_t mode);
ssize_t emu_read(int fd, void* buf, size_t count);
ssize_t emu_write(int fd, const void* buf, size_t count);
off_t emu_lseek(int fd, off_t offset, int whence);
int emu_fstat(int fd, struct stat* s);
int emu_stat(const char* fn, struct stat* s);
DIR* emu_opendir(const char* name);
struct dirent* emu_readdir(DIR* dirp);
int emu_closedir(DIR* dirp);
