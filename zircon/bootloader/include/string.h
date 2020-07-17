// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_STRING_H_
#define ZIRCON_BOOTLOADER_INCLUDE_STRING_H_

#include <stddef.h>

void* memset(void* dst, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t max);
char* strchr(const char* s, int c);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t len);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t len);
size_t strspn(const char* s, const char* accept);
char* strpbrk(const char* cs, const char* ct);
char* strtok(char* s, const char* ct);

#endif  // ZIRCON_BOOTLOADER_INCLUDE_STRING_H_
