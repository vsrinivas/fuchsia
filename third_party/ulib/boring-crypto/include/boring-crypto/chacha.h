/* Copyright (c) 2014, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS

/* CRYPTO_chacha_20 encrypts |in_len| bytes from |in| with the given key and
 * nonce and writes the result to |out|. If |in| and |out| alias, they must be
 * equal. The initial block counter is specified by |counter|. */
void CRYPTO_chacha_20(uint8_t* out, const uint8_t* in,
                      size_t in_len, const uint8_t key[32],
                      const uint8_t nonce[12], uint32_t counter);

__END_CDECLS
