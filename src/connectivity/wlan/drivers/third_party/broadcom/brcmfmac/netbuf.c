/*
 * Copyright (c) 2018 The Fuchsia Authors
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
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "netbuf.h"

#include <stdlib.h>
#include <sys/types.h>

#include "debug.h"

struct brcmf_netbuf* brcmf_netbuf_allocate(uint32_t size) {
    struct brcmf_netbuf* netbuf = calloc(1, sizeof(*netbuf));
    if (netbuf == NULL) {
        return NULL;
    }
    netbuf->data = netbuf->allocated_buffer = malloc(size);
    if (netbuf->data == NULL) {
        free(netbuf);
        return NULL;
    }
    netbuf->allocated_size = size;
    return netbuf;
}

void brcmf_netbuf_free(struct brcmf_netbuf* netbuf) {
    free(netbuf->allocated_buffer);
    free(netbuf);
}
