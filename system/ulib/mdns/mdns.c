// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L // for strnlen

#include <mdns/mdns.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void mdns_init_message(mdns_message* m) {
    memset(m, 0, sizeof(mdns_message));
}

void mdns_free_message(mdns_message* m) {
    mdns_question* next;

    // Free all questions in this message.
    while (m->questions != NULL) {
        next = m->questions->next;
        free(m->questions);
        m->questions = next;
    }

    mdns_init_message(m);
}

int mdns_add_question(mdns_message* m,
                      const char* domain,
                      uint16_t qtype,
                      uint16_t qclass) {
    if (strnlen(domain, MAX_DOMAIN_LENGTH) == MAX_DOMAIN_LENGTH) {
        errno = ENAMETOOLONG;
        return -1;
    }

    mdns_question* q;
    uint16_t qd_count = 0;

    // Find insertion point for the new question.
    if (m->questions == NULL) {
        m->questions = calloc(1, sizeof(mdns_question));
        if (m->questions == NULL) {
            errno = ENOMEM;
            return -1;
        }
        q = m->questions;
    } else {
        q = m->questions;
        qd_count++;
        while (q->next != NULL) {
            q = q->next;
            qd_count++;
        }
        q->next = calloc(1, sizeof(mdns_question));
        if (q->next == NULL) {
            errno = ENOMEM;
            return -1;
        }
        q = q->next;
    }

    qd_count++;

    // Init the new question.
    strncpy(q->domain, domain, MAX_DOMAIN_LENGTH);
    q->qtype = qtype;
    q->qclass = qclass;
    q->next = NULL;

    // Fixup the message header
    m->header.qd_count = qd_count;
    return 0;
}
