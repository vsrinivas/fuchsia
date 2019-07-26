// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct kvstore kvstore_t;

#define KVS_OK 0
#define KVS_ERR_INTERNAL -1
#define KVS_ERR_BAD_PARAM -2
#define KVS_ERR_OUT_OF_SPACE -3
#define KVS_ERR_NOT_FOUND -4
#define KVS_ERR_PARSE_HDR -5
#define KVS_ERR_PARSE_REC -6
#define KVS_ERR_PARSE_CRC -7

// KVStore API
// -----------

// Setup a new, empty kvstore, backed by buffer.
void kvs_init(kvstore_t* kvs, void* buffer, size_t buflen);

// Initialize a kvstore (read from disk, etc), backed by buffer.
int kvs_load(kvstore_t* kvs, void* buffer, size_t buflen);

// Prepare kvstore for saving (compute checksum & update header).
// On success kvs->data and kvs->datalen represents the data
// to write to storage.
int kvs_save(kvstore_t* kvs);

// Adds a new key and value, provided there is space.
// Does not check for duplicates.
int kvs_addn(kvstore_t* kvs, const void* key, size_t klen, const void* val, size_t vlen);

// Adds a new key and value, provided there is space.
// Does not check for duplicates.
int kvs_add(kvstore_t* kvs, const char* key, const char* value);

// Locates key and returns its value and OK, else NOT_FOUND
// returned pointer is not guaranteed stable if kvstore is mutated.
int kvs_getn(kvstore_t* kvs, const void* key, size_t klen, const void** val, size_t* vlen);

// Locates key and returns its value if found, otherwise returns fallback
// returned pointer is not guaranteed stable if kvstore is mutated.
const char* kvs_get(kvstore_t* kvs, const char* key, const char* fallback);

// Calls func() for each key/value pair.
// Return KVS_OK at the end, or stops and returns whatever func()
// returned. if func returns non-zero.
int kvs_foreach(kvstore_t* kvs, void* cookie,
                int (*func)(void* cookie, const char* key, const char* val));

// KVStore Wire Format and Internals
// ---------------------------------

// <header> <kventry>* [ <signature> ]
//
// <header> := <u64:version> <u32:flags> <u32:length> <u32:crc32> <u32:reserved>
// <kventry> := <u8:klen> <u8:vlen> <u8[klen]:key> <u8:0> <u8[vlen]:value> <u8:0>
// <signature> := TBD

// echo -n "kvstore-version-1" | sha256sum (LSB)
#define KVSTORE_VERSION 0x540f19caa7bf19dcUL

#define KVSTORE_FLAG_SIGNED 1

struct kvstore {
  void* data;
  size_t datalen;
  size_t datamax;
  size_t kvcount;
};

typedef struct kvshdr {
  uint64_t version;
  uint32_t flags;
  uint32_t length;
  uint32_t reserved;
  uint32_t crc;
} kvshdr_t;

__END_CDECLS
