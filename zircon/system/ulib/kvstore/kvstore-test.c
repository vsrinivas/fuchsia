// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>

#include <kvstore/kvstore.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

static bool kvs_bad_args(void) {
  BEGIN_TEST;

  kvstore_t kvs;
  uint8_t buffer[1024];

  char str[300];
  memset(str, 'a', 299);
  str[299] = 0;

  // kvstore too small for even the header
  kvs_init(&kvs, buffer, 3);
  ASSERT_EQ(kvs_save(&kvs), KVS_ERR_OUT_OF_SPACE, "");

  ASSERT_EQ(kvs_add(&kvs, "key", "value"), KVS_ERR_OUT_OF_SPACE, "");

  // too-large keys or values
  ASSERT_EQ(kvs_add(&kvs, str, "value"), KVS_ERR_BAD_PARAM, "");
  ASSERT_EQ(kvs_add(&kvs, "key", str), KVS_ERR_BAD_PARAM, "");

  str[256] = 0;
  // just one byte too large
  ASSERT_EQ(kvs_add(&kvs, str, "value"), KVS_ERR_BAD_PARAM, "");
  ASSERT_EQ(kvs_add(&kvs, "key", str), KVS_ERR_BAD_PARAM, "");

  // empty keys are invalid
  ASSERT_EQ(kvs_add(&kvs, "", "value"), KVS_ERR_BAD_PARAM, "");

  END_TEST;
}

static int kvs_check(kvstore_t* kvs, const char* key, const char* val) {
  const char* out = kvs_get(kvs, key, NULL);
  if (out == NULL) {
    return KVS_ERR_NOT_FOUND;
  }
  if (strcmp(val, out)) {
    return KVS_ERR_INTERNAL;
  }
  return KVS_OK;
}

static int kvs_verify(kvstore_t* kvs, const void* data, size_t dlen, size_t count) {
  if (memcmp(kvs->data + sizeof(kvshdr_t), data, dlen)) {
    printf("\ndata mismatch between kvs (first) and expected (second):\n");
    hexdump8(kvs->data + sizeof(kvshdr_t), dlen);
    hexdump8(data, dlen);
    return KVS_ERR_INTERNAL;
  }
  if ((kvs->datalen - sizeof(kvshdr_t)) != dlen) {
    return KVS_ERR_BAD_PARAM;
  }
  if (kvs->kvcount != count) {
    return KVS_ERR_BAD_PARAM;
  }
  return KVS_OK;
}

static bool kvs_get_put(void) {
  BEGIN_TEST;

  kvstore_t kvs;
  uint8_t buffer[2048];
  memset(buffer, '@', sizeof(buffer));

  char str[256];
  memset(str, 'a', 255);
  str[255] = 0;

  kvs_init(&kvs, buffer, sizeof(buffer));

  // simple
  ASSERT_EQ(kvs_add(&kvs, "key1", "val1"), KVS_OK, "");
  ASSERT_EQ(kvs_verify(&kvs, "\x04\x04key1\0val1\0", 12, 1), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key1", "val1"), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "key2", "val2"), KVS_OK, "");
  ASSERT_EQ(kvs_verify(&kvs, "\x04\x04key1\0val1\0\x04\x04key2\0val2\0", 24, 2), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key1", "val1"), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key2", "val2"), KVS_OK, "");

  // max allowable key/value
  ASSERT_EQ(kvs_add(&kvs, str, "value"), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, str, "value"), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "key", str), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key", str), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, str, str), KVS_OK, "");

  END_TEST;
}

static bool kvs_wire_format(void) {
  BEGIN_TEST;

  const uint8_t content[] =
      "\x04\x04key1\0aaaa\0\x04\x08key2\0abcdefgh\0\x06\x00keykey\0\0\x04\x04key4\0bbbb";
  kvshdr_t hdr = {
      .version = KVSTORE_VERSION,
      .flags = 0,
      .length = sizeof(kvshdr_t) + sizeof(content),
      .crc = 0,
      .reserved = 0,
  };

  kvstore_t kvs;
  uint8_t buffer[1024];

  hdr.crc = crc32(0, (const void*)&hdr, sizeof(hdr) - sizeof(uint32_t));
  hdr.crc = crc32(hdr.crc, content, sizeof(content));
  memcpy(buffer, &hdr, sizeof(hdr));
  memcpy(buffer + sizeof(hdr), content, sizeof(content));

  // Create a new kvs with the same content, save it, compare raw data
  uint8_t buffer2[1024];
  kvs_init(&kvs, buffer2, sizeof(buffer2));
  ASSERT_EQ(kvs_add(&kvs, "key1", "aaaa"), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "key2", "abcdefgh"), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "keykey", ""), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "key4", "bbbb"), KVS_OK, "");
  ASSERT_EQ(kvs_save(&kvs), KVS_OK, "");
  ASSERT_EQ(kvs.datalen, sizeof(hdr) + sizeof(content), "");
  ASSERT_EQ(memcmp(buffer, buffer2, kvs.datalen), 0, "");

  // mutated data should fail due to crc check
  buffer[sizeof(hdr) + 8] = 0x42;
  ASSERT_EQ(kvs_load(&kvs, buffer, sizeof(hdr) + sizeof(content)), KVS_ERR_PARSE_CRC, "");

  // exactly sized should parse
  memcpy(buffer, &hdr, sizeof(hdr));
  memcpy(buffer + sizeof(hdr), content, sizeof(content));
  ASSERT_EQ(kvs_load(&kvs, buffer, sizeof(hdr) + sizeof(content)), KVS_OK, "");

  // verify we can find all the keys
  ASSERT_EQ(kvs_check(&kvs, "key1", "aaaa"), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key2", "abcdefgh"), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "keykey", ""), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key4", "bbbb"), KVS_OK, "");

  // but there's no space left
  ASSERT_EQ(kvs_add(&kvs, "newkey", "newval"), KVS_ERR_OUT_OF_SPACE, "");

  // larger buffer should allow keys to be added
  memcpy(buffer, &hdr, sizeof(hdr));
  memcpy(buffer + sizeof(hdr), content, sizeof(content));
  ASSERT_EQ(kvs_load(&kvs, buffer, sizeof(buffer)), KVS_OK, "");

  // add additional keys
  ASSERT_EQ(kvs_add(&kvs, "key000000", "val000000"), KVS_OK, "");
  ASSERT_EQ(kvs_add(&kvs, "key000001", "val000001"), KVS_OK, "");

  const uint8_t newcontent[] = "\x09\x09key000000\0val000000\0\x09\x09key000001\0val000001";
  hdr.crc = crc32(0, (const void*)&hdr, sizeof(hdr) - sizeof(uint32_t));
  hdr.crc = crc32(hdr.crc, content, sizeof(content));

  uint8_t checkbuf[sizeof(content) + sizeof(newcontent)];
  memcpy(checkbuf, content, sizeof(content));
  memcpy(checkbuf + sizeof(content), newcontent, sizeof(newcontent));
  ASSERT_EQ(kvs_verify(&kvs, checkbuf, sizeof(checkbuf), 6), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key000000", "val000000"), KVS_OK, "");
  ASSERT_EQ(kvs_check(&kvs, "key000001", "val000001"), KVS_OK, "");

  // truncated buffer should fail
  memcpy(buffer, &hdr, sizeof(hdr));
  memcpy(buffer + sizeof(hdr), content, sizeof(content));
  ASSERT_EQ(kvs_load(&kvs, buffer, sizeof(hdr) + sizeof(content) - 1), KVS_ERR_PARSE_HDR, "");

  // truncated records should fail
  hdr.length -= 3;
  hdr.crc = crc32(0, (const void*)&hdr, sizeof(hdr) - sizeof(uint32_t));
  hdr.crc = crc32(hdr.crc, content, sizeof(content) - 3);
  memcpy(buffer, &hdr, sizeof(hdr));
  memcpy(buffer + sizeof(hdr), content, sizeof(content));
  ASSERT_EQ(kvs_load(&kvs, buffer, sizeof(hdr) + sizeof(content) - 3), KVS_ERR_PARSE_REC, "");

  END_TEST;
}

BEGIN_TEST_CASE(kvstore_tests)
RUN_TEST(kvs_bad_args)
RUN_TEST(kvs_get_put)
RUN_TEST(kvs_wire_format)
END_TEST_CASE(kvstore_tests)
