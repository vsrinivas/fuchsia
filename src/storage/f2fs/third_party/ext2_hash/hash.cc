/*
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD)
 *
 * Copyright (c) 2010, 2013 Zheng Liu <lz@freebsd.org>
 * Copyright (c) 2012, Vyacheslav Matyushin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

namespace f2fs {

void TEATransform(unsigned int buf[4], unsigned int const in[]) {
  uint32_t tea_delta = 0x9E3779B9;
  uint32_t sum;
  uint32_t x = buf[0], y = buf[1];
  int n = 16;
  int i = 1;

  while (n-- > 0) {
    sum = i * tea_delta;
    x += ((y << 4) + in[0]) ^ (y + sum) ^ ((y >> 5) + in[1]);
    y += ((x << 4) + in[2]) ^ (x + sum) ^ ((x >> 5) + in[3]);
    i++;
  }

  buf[0] += x;
  buf[1] += y;
}

void Str2HashBuf(const char *msg, int len, unsigned int *buf, int num) {
  uint32_t padding = len | (len << 8) | (len << 16) | (len << 24);
  uint32_t buf_val;
  int i;

  if (len > num * 4)
    len = num * 4;

  buf_val = padding;

  for (i = 0; i < len; i++) {
    if ((i % 4) == 0)
      buf_val = padding;

    buf_val <<= 8;
    buf_val += msg[i];

    if ((i % 4) == 3) {
      *buf++ = buf_val;
      num--;
      buf_val = padding;
    }
  }

  if (--num >= 0)
    *buf++ = buf_val;

  while (--num >= 0)
    *buf++ = padding;
}

}  // namespace f2fs
