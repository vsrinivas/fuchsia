// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L // for strnlen

#include <mdns/mdns.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

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

    mdns_rr* next_rr;

    // Free all answers in this message.
    while (m->answers != NULL) {
        next_rr = m->answers->next;
        free(m->answers->name);
        free(m->answers);
        m->answers = next_rr;
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

bool is_valid_rr_type(uint16_t type) {
    return type == RR_TYPE_A ||
           type == RR_TYPE_AAAA;
}

bool is_valid_rr_class(uint16_t clazz) {
    return clazz == RR_CLASS_IN;
}

// Appends a resource record to the given linked list. See internal.h for docs.
int mdns_add_rr(mdns_rr** rrsptr,
                char* name,
                uint16_t type,
                uint16_t clazz,
                uint8_t* rdata,
                uint16_t rdlength,
                uint32_t ttl) {
    if (!(is_valid_rr_type(type) && is_valid_rr_class(clazz))) {
        errno = EINVAL;
        return -1;
    }

    mdns_rr* rr;
    int rr_count = 0;

    // Find insertion point.
    if (*rrsptr == NULL) {
        *rrsptr = calloc(1, sizeof(mdns_rr));
        if (*rrsptr == NULL) {
            errno = ENOMEM;
            return -1;
        }
        rr = *rrsptr;
    } else {
        rr = *rrsptr;
        rr_count++;

        while (rr->next != NULL) {
            rr = rr->next;
            rr_count++;
        }
        rr->next = calloc(1, sizeof(mdns_rr));
        if (rr->next == NULL) {
            errno = ENOMEM;
            return -1;
        }
        rr = rr->next;
    }

    // Init the new resource record
    strncpy(rr->name, name, MAX_DOMAIN_LENGTH);
    rr->type = type;
    rr->clazz = clazz;
    rr->rdata = rdata;
    rr->rdlength = rdlength;
    rr->ttl = ttl;

    return ++rr_count;
}

int mdns_add_answer(mdns_message* m,
                    char* name,
                    uint16_t type,
                    uint16_t clazz,
                    uint8_t* rdata,
                    uint16_t rdlength,
                    uint32_t ttl) {
    int an_count = mdns_add_rr(&(m->answers), name, type, clazz, rdata,
                               rdlength, ttl);
    if (an_count < 0) {
        return an_count;
    }
    m->header.an_count = an_count;
    return 0;
}

int mdns_add_authority(mdns_message* m,
                       char* name,
                       uint16_t type,
                       uint16_t clazz,
                       uint8_t* rdata,
                       uint16_t rdlength,
                       uint32_t ttl) {
    int ns_count = mdns_add_rr(&(m->authorities), name, type, clazz, rdata,
                               rdlength, ttl);
    if (ns_count < 0) {
        return ns_count;
    }
    m->header.ns_count = ns_count;
    return 0;
}

int mdns_add_additional(mdns_message* m,
                        char* name,
                        uint16_t type,
                        uint16_t clazz,
                        uint8_t* rdata,
                        uint16_t rdlength,
                        uint32_t ttl) {
    int ar_count = mdns_add_rr(&(m->additionals), name, type, clazz, rdata,
                               rdlength, ttl);
    if (ar_count < 0) {
        return ar_count;
    }
    m->header.ar_count = ar_count;
    return 0;
}

int mdns_unmarshal(const void* buf,
                   const size_t buf_len,
                   mdns_message* container) {
    // Total number of bytes read during unmarshalling.
    int read_count = 0;

    mdns_init_message(container);

    // It's impossible to decode a message that doesn't contain a full header.
    if (buf_len < MDNS_HEADER_SIZE) {
        errno = EBADMSG;
        return -1;
    }

    read_count += unmarshal_header(buf, &(container->header));

    // TODO(kjharland): Unmarshal other sections.
    return read_count;
}

int unmarshal_header(const void* buf,
                     mdns_header* container) {
    memcpy(container, buf, MDNS_HEADER_SIZE);
    return MDNS_HEADER_SIZE;
}
