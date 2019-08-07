// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/cksum.h>
#include <string.h>

#include <kvstore/kvstore.h>

// header, key, zero, value, zero
#define RECLEN(ksz, vsz) (2 + (ksz) + 1 + (vsz) + 1)

static_assert(sizeof(kvshdr_t) == 6 * sizeof(uint32_t), "");

void kvs_init(kvstore_t* kvs, void* buffer, size_t buflen) {
  kvs->data = buffer;
  kvs->datamax = buflen;
  kvs->kvcount = 0;
  if (buflen < sizeof(kvshdr_t)) {
    kvs->datalen = kvs->datamax;
  } else {
    kvs->datalen = sizeof(kvshdr_t);
  }
}

int kvs_load(kvstore_t* kvs, void* buffer, size_t buflen) {
  // initially configure kvstore as invalid to provide
  // some protection against using after ignoring the
  // return value of load
  kvs->data = buffer;
  kvs->datalen = buflen;
  kvs->datamax = buflen;
  kvs->kvcount = 0;

  kvshdr_t hdr;
  if (buflen < sizeof(hdr)) {
    return KVS_ERR_BAD_PARAM;
  }

  memcpy(&hdr, buffer, sizeof(hdr));
  if ((hdr.version != KVSTORE_VERSION) || (hdr.length < sizeof(hdr))) {
    return KVS_ERR_PARSE_HDR;
  }
  if (hdr.length > buflen) {
    return KVS_ERR_PARSE_HDR;
  }
  if (hdr.flags != 0) {
    return KVS_ERR_PARSE_HDR;
  }
  if (hdr.reserved != 0) {
    return KVS_ERR_PARSE_HDR;
  }

  uint32_t crc = crc32(0, buffer, sizeof(hdr) - sizeof(uint32_t));
  crc = crc32(crc, buffer + sizeof(hdr), hdr.length - sizeof(hdr));
  if (crc != hdr.crc) {
    return KVS_ERR_PARSE_CRC;
  }

  size_t count = 0;
  uint8_t* kv = buffer + sizeof(hdr);
  uint8_t* rec = kv;
  size_t avail = hdr.length - sizeof(hdr);
  while (avail > 0) {
    if (avail < 2) {
      return KVS_ERR_PARSE_REC;
    }
    size_t klen = rec[0];
    size_t vlen = rec[1];
    size_t reclen = RECLEN(klen, vlen);
    if (avail < reclen) {
      return KVS_ERR_PARSE_REC;
    }
    if (rec[2 + klen] != 0) {
      return KVS_ERR_PARSE_REC;
    }
    if (rec[2 + klen + 1 + vlen] != 0) {
      return KVS_ERR_PARSE_REC;
    }
    rec += reclen;
    avail -= reclen;
    count++;
  }

  kvs->kvcount = count;
  kvs->datalen = sizeof(hdr) + (rec - kv);
  return KVS_OK;
}

int kvs_save(kvstore_t* kvs) {
  if (kvs->datamax < sizeof(kvshdr_t)) {
    return KVS_ERR_OUT_OF_SPACE;
  }
  kvshdr_t hdr;
  hdr.version = KVSTORE_VERSION;
  hdr.flags = 0;
  hdr.length = kvs->datalen;
  hdr.reserved = 0;
  hdr.crc = crc32(0, (const void*)&hdr, sizeof(hdr) - sizeof(uint32_t));
  hdr.crc = crc32(hdr.crc, kvs->data + sizeof(hdr), hdr.length - sizeof(hdr));
  memcpy(kvs->data, &hdr, sizeof(hdr));
  return KVS_OK;
}

int kvs_addn(kvstore_t* kvs, const void* key, size_t klen, const void* val, size_t vlen) {
  // ensure valid parameters
  if ((klen == 0) || (klen > 255) || (vlen > 255)) {
    return KVS_ERR_BAD_PARAM;
  }

  // ensure available space
  size_t reclen = RECLEN(klen, vlen);
  if (reclen > (kvs->datamax - kvs->datalen)) {
    return KVS_ERR_OUT_OF_SPACE;
  }

  uint8_t* rec = kvs->data + kvs->datalen;
  *rec++ = klen;
  *rec++ = vlen;
  memcpy(rec, key, klen);
  rec += klen;
  *rec++ = 0;
  memcpy(rec, val, vlen);
  rec += vlen;
  *rec++ = 0;

  kvs->datalen += reclen;
  kvs->kvcount++;
  return KVS_OK;
}

int kvs_add(kvstore_t* kvs, const char* key, const char* value) {
  return kvs_addn(kvs, key, strlen(key), value, strlen(value));
}

int kvs_getn(kvstore_t* kvs, const void* key, size_t klen, const void** val, size_t* vlen) {
  uint8_t* rec = kvs->data + sizeof(kvshdr_t);
  size_t count;
  for (count = 0; count < kvs->kvcount; count++) {
    size_t ksz = rec[0];
    size_t vsz = rec[1];
    if ((klen == ksz) && !memcmp(key, rec + 2, klen)) {
      *val = rec + 2 + klen + 1;
      if (vlen) {
        *vlen = vsz;
      }
      return KVS_OK;
    }
    rec += RECLEN(ksz, vsz);
  }
  return KVS_ERR_NOT_FOUND;
}

const char* kvs_get(kvstore_t* kvs, const char* key, const char* fallback) {
  const void* val;
  if (kvs_getn(kvs, key, strlen(key), &val, NULL) == KVS_OK) {
    return (const char*)val;
  } else {
    return fallback;
  }
}

int kvs_foreach(kvstore_t* kvs, void* cookie,
                int (*func)(void* cookie, const char* key, const char* val)) {
  uint8_t* rec = kvs->data + sizeof(kvshdr_t);
  size_t count;
  for (count = 0; count < kvs->kvcount; count++) {
    size_t ksz = rec[0];
    size_t vsz = rec[1];
    int r = func(cookie, (const char*)(rec + 2), (const char*)(rec + 2 + ksz + 1));
    if (r != KVS_OK) {
      return r;
    }
    rec += RECLEN(ksz, vsz);
  }
  return KVS_OK;
}
