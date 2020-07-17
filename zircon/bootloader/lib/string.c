// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>

void* memset(void* _dst, int c, size_t n) {
  uint8_t* dst = _dst;
  while (n-- > 0) {
    *dst++ = c;
  }
  return _dst;
}

void* memcpy(void* _dst, const void* _src, size_t n) {
  uint8_t* dst = _dst;
  const uint8_t* src = _src;
  while (n-- > 0) {
    *dst++ = *src++;
  }
  return _dst;
}

int memcmp(const void* _a, const void* _b, size_t n) {
  const uint8_t* a = _a;
  const uint8_t* b = _b;
  while (n-- > 0) {
    int x = *a++ - *b++;
    if (x != 0) {
      return x;
    }
  }
  return 0;
}

size_t strlen(const char* s) {
  size_t len = 0;
  while (*s++)
    len++;
  return len;
}

size_t strnlen(const char* s, size_t max) {
  size_t len = 0;
  while (len < max && *s++)
    len++;
  return len;
}

char* strchr(const char* s, int c) {
  while (*s != c && *s++)
    ;
  if (*s != c)
    return 0;
  return (char*)s;
}

char* strcpy(char* dst, const char* src) {
  while (*src != 0) {
    *dst++ = *src++;
  }
  return dst;
}

char* strncpy(char* dst, const char* src, size_t len) {
  while (len-- > 0 && *src != 0) {
    *dst++ = *src++;
  }
  return dst;
}

int strcmp(const char* s1, const char* s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *s1 - *s2;
}

int strncmp(const char* s1, const char* s2, size_t len) {
  while (len-- > 0) {
    int diff = *s1 - *s2;
    if (diff != 0 || *s1 == '\0') {
      return diff;
    }
    s1++;
    s2++;
  }
  return 0;
}

char* strpbrk(char const* cs, char const* ct) {
  for (const char* sc1 = cs; *sc1 != '\0'; ++sc1) {
    for (const char* sc2 = ct; *sc2 != '\0'; ++sc2) {
      if (*sc1 == *sc2)
        return (char*)sc1;
    }
  }

  return NULL;
}

size_t strspn(char const* s, char const* accept) {
  size_t count = 0;

  for (const char *p = s; *p != '\0'; ++p) {
    const char *a;
    for (a = accept; *a != '\0'; ++a) {
      if (*p == *a)
        break;
    }
    if (*a == '\0')
      return count;
    ++count;
  }

  return count;
}

static char* ___strtok = NULL;

char* strtok(char* s, char const* ct) {
  char *sbegin, *send;

  sbegin = s ? s : ___strtok;
  if (!sbegin) {
    return NULL;
  }
  sbegin += strspn(sbegin, ct);
  if (*sbegin == '\0') {
    ___strtok = NULL;
    return NULL;
  }
  send = strpbrk(sbegin, ct);
  if (send && *send != '\0')
    *send++ = '\0';
  ___strtok = send;
  return sbegin;
}
