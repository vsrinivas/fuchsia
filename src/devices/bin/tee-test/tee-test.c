// Copyright 2018-2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <tee-client-api/tee_client_api.h>

//
// `fx shell tee-test`:
//
// Use this command to verify Fuchsia/TEE communications and TA loading on
// real hardware.
//
// Examples:
//
// # Verify basic Fuchsia/TEE communications is working by loading and
// # invoking a simple command of the "Hello World" TA.
// $> fx shell tee-test
//
// # Verify if a specific TA is signed properly (loads successfully).
// # <ta-uuid> is the UUID of the TA in canonical format, e.g.:
// # "abcd1234-abcd-abcd-abcd-abcd1234abcd"
// $> fx shell tee-test <ta-uuid>
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define RETURN_STR_IF_MATCH(v, code) \
  do {                               \
    if (v == code)                   \
      return #code;                  \
  } while (0)

static const TEEC_UUID ta_helloworld_uuid = {
    0x8aaaf200, 0x2450, 0x11e4, {0xab, 0xe2, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

static const char *result_to_str(TEEC_Result result) {
  RETURN_STR_IF_MATCH(result, TEEC_SUCCESS);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_GENERIC);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_ACCESS_DENIED);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_CANCEL);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_ACCESS_CONFLICT);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_EXCESS_DATA);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_BAD_FORMAT);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_BAD_PARAMETERS);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_BAD_STATE);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_ITEM_NOT_FOUND);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_NOT_IMPLEMENTED);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_NOT_SUPPORTED);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_NO_DATA);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_OUT_OF_MEMORY);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_BUSY);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_COMMUNICATION);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_SECURITY);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_SHORT_BUFFER);
  RETURN_STR_IF_MATCH(result, TEE_ERROR_EXTERNAL_CANCEL);
  RETURN_STR_IF_MATCH(result, TEE_ERROR_OVERFLOW);
  RETURN_STR_IF_MATCH(result, TEE_ERROR_TARGET_DEAD);
  RETURN_STR_IF_MATCH(result, TEEC_ERROR_TARGET_DEAD);
  RETURN_STR_IF_MATCH(result, TEE_ERROR_STORAGE_NO_SPACE);

  static char buf[12];
  sprintf(buf, "0x%08x", result);
  return buf;
}

static const char *origin_to_str(uint32_t origin) {
  RETURN_STR_IF_MATCH(origin, TEEC_ORIGIN_API);
  RETURN_STR_IF_MATCH(origin, TEEC_ORIGIN_COMMS);
  RETURN_STR_IF_MATCH(origin, TEEC_ORIGIN_TEE);
  RETURN_STR_IF_MATCH(origin, TEEC_ORIGIN_TRUSTED_APP);

  static char buf[12];
  sprintf(buf, "0x%08x", origin);
  return buf;
}

// Parses an UUID in canonical representation, e.g.:
// "abcd1234-abcd-abcd-abcd-abcd1234abcd"
static int uuid_parse(const char *in, TEEC_UUID *out) {
  int n_fields;
  int n_chars = 0;

  n_fields = sscanf(in, "%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%n",
                    &out->timeLow, &out->timeMid, &out->timeHiAndVersion, &out->clockSeqAndNode[0],
                    &out->clockSeqAndNode[1], &out->clockSeqAndNode[2], &out->clockSeqAndNode[3],
                    &out->clockSeqAndNode[4], &out->clockSeqAndNode[5], &out->clockSeqAndNode[6],
                    &out->clockSeqAndNode[7], &n_chars);

  if (!(n_fields == 11 && n_chars == 36)) {
    printf("UUID (%s) not in expected format (%s).\n", in, "08x-04x-04x-04x-012x");
    return -1;
  }

  return 0;
}

static int uuid_compare(const TEEC_UUID *lh, const TEEC_UUID *rh) {
  return memcmp(lh, rh, sizeof(TEEC_UUID));
}

#define TA_HELLO_WORLD_CMD_INC_VALUE 0
static TEEC_Result ta_helloworld_invoke_cmds(TEEC_Session *session) {
  TEEC_Result result;
  uint32_t origin;
  TEEC_Operation op = {0};

  op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
  op.params[0].value.a = 42;

  result = TEEC_InvokeCommand(session, TA_HELLO_WORLD_CMD_INC_VALUE, &op, &origin);
  if (result != TEEC_SUCCESS) {
    printf("TEEC_InvokeCommand failed. Result=%s, origin=%s.\n", result_to_str(result),
           origin_to_str(origin));
    return result;
  }

  if (result == TEEC_SUCCESS && op.params[0].value.a != 43) {
    return TEEC_ERROR_GENERIC;
  }

  return result;
}

static int test_ta(const TEEC_UUID *ta_uuid) {
  TEEC_Result result;
  TEEC_Context context;
  TEEC_Session session;
  uint32_t origin;

  result = TEEC_InitializeContext(NULL, &context);
  if (result != TEEC_SUCCESS) {
    printf("Failed to initialize context. Result=%s.\n", result_to_str(result));
    return 1;
  }

  result = TEEC_OpenSession(&context, &session, ta_uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
  if (result != TEEC_SUCCESS) {
    printf("Failed to open session. Result=%s, origin=%s\n", result_to_str(result),
           origin_to_str(origin));
    TEEC_FinalizeContext(&context);
    return 1;
  }

  if (uuid_compare(ta_uuid, &ta_helloworld_uuid) == 0) {
    result = ta_helloworld_invoke_cmds(&session);
  }

  TEEC_CloseSession(&session);
  TEEC_FinalizeContext(&context);

  return (result == TEEC_SUCCESS) ? 0 : 1;
}

int main(int argc, char *argv[]) {
  TEEC_UUID ta_uuid = ta_helloworld_uuid;

  if (argc > 1 && uuid_parse(argv[1], &ta_uuid)) {
    printf("Bad UUID: %s\n", argv[1]);
    return 1;
  }

  return test_ta(&ta_uuid);
}
