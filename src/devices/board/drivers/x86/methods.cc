// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "methods.h"

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zircon/types.h>

#include <acpica/acpi.h>
#include <acpica/acuuid.h>

#include "errors.h"
#include "util.h"

static zx_status_t uuid_str_to_uint8_buf(const char* uuid_str, uint8_t* uuid) {
  if (strlen(uuid_str) != 36) {
    return ZX_ERR_WRONG_TYPE;
  }

  // Converts the format string aabbccdd-eeff-gghh-iijj-kkllmmnnoopp to
  // { dd, cc, bb, aa, ff, ee, hh, gg, ii, jj, kk, ll, mm, nn, oo, pp }
  // per ACPI Spec 6.1, 19.6.136
  int ret =
      sscanf(uuid_str,
             "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "-%02" SCNx8 "%02" SCNx8 "-%02" SCNx8
             "%02" SCNx8 "-%02" SCNx8 "%02" SCNx8 "-%02" SCNx8 "%02" SCNx8 "%02" SCNx8 "%02" SCNx8
             "%02" SCNx8 "%02" SCNx8,
             &uuid[3], &uuid[2], &uuid[1], &uuid[0], &uuid[5], &uuid[4], &uuid[7], &uuid[6],
             &uuid[8], &uuid[9], &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14], &uuid[15]);
  if (ret != 16) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// Call the ACPI _BNN method to query a PCI Host Bridge's base bus number.
zx_status_t acpi_bbn_call(ACPI_HANDLE dev_obj, uint8_t* out_bbn) {
  uint64_t tmp = 0;
  ACPI_STATUS status = acpi_evaluate_integer(dev_obj, "_BBN", &tmp);

  // BBN is returned in the lower 8 bits
  *out_bbn = tmp & 0xFF;
  return acpi_to_zx_status(status);
}

// Call the ACPI _CRT method to query critical shutdown temperature.
zx_status_t acpi_crt_call(ACPI_HANDLE dev_obj, uint64_t* out) {
  ACPI_STATUS status = acpi_evaluate_integer(dev_obj, "_CRT", out);
  return acpi_to_zx_status(status);
}

enum {
  OSC_RET_FAILURE = (1u << 1),
  OSC_RET_BAD_UUID = (1u << 2),
  OSC_RET_BAD_REV = (1u << 3),
  OSC_RET_MASKED = (1u << 4),
};

// Check for the 3 bits that indicate a failure in calling _OSC
static constexpr bool osc_bad_result(uint32_t val) {
  return (val & (OSC_RET_FAILURE | OSC_RET_BAD_UUID | OSC_RET_BAD_REV));
}

// Call the ACPI _OSC method to query and negotiate OS capabilities.
zx_status_t acpi_osc_call(ACPI_HANDLE dev_obj, const char* uuid_str, uint64_t revision,
                          size_t dword_cnt, uint32_t* dwords_in, uint32_t* dwords_out,
                          bool* bit_masked) {
  // The _OSC spec in 6.2.11 requires at least 2 dwords, though some specific invocations such
  // as PCIe require 3+.
  if (!dwords_in || !dwords_out || dword_cnt < 2) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t uuid[16] = {};
  if (uuid_str_to_uint8_buf(uuid_str, uuid) != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }

  ACPI_OBJECT objs[4] = {};
  uint32_t dword_length = static_cast<uint32_t>(dword_cnt * sizeof(uint32_t));

  // UUID
  objs[0].Buffer.Type = ACPI_TYPE_BUFFER;
  objs[0].Buffer.Length = ACPI_UUID_SIZE;
  objs[0].Buffer.Pointer = (uint8_t*)uuid;

  // revision id
  objs[1].Integer.Type = ACPI_TYPE_INTEGER;
  objs[1].Integer.Value = revision;

  // number of dwords in the next arg
  objs[2].Integer.Type = ACPI_TYPE_INTEGER;
  objs[2].Integer.Value = dword_cnt;

  // buffer containing dwords
  objs[3].Buffer.Type = ACPI_TYPE_BUFFER;
  objs[3].Buffer.Length = dword_length;
  objs[3].Buffer.Pointer = (uint8_t*)dwords_in;

  ACPI_OBJECT_LIST params = {};
  params.Count = countof(objs);
  params.Pointer = objs;

  // Have ACPI allocate the return buffer for us.
  ACPI_BUFFER out = {};
  out.Length = ACPI_ALLOCATE_BUFFER;
  out.Pointer = NULL;

  // Make the call and ensure that both the rpc itself and the status bits returned
  // in the first dword all indicate success.
  ACPI_STATUS acpi_status = AcpiEvaluateObject(dev_obj, const_cast<char*>("_OSC"), &params, &out);
  if (acpi_status != AE_OK) {
    printf("error making _OSC call: %d!\n", acpi_status);
    return acpi_to_zx_status(acpi_status);
  }

  // Ensure we free ACPI's memory allocation for the _OSC call.
  auto acpi_object_free = fit::defer([&]() { AcpiOsFree(out.Pointer); });
  ACPI_OBJECT* out_obj = static_cast<ACPI_OBJECT*>(out.Pointer);
  if (out_obj->Buffer.Length > dword_length) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(dwords_out, out_obj->Buffer.Pointer, out_obj->Buffer.Length);
  // Inform the caller if a bit was masked off in negotiation of capabilities
  *bit_masked = dwords_out[0] & OSC_RET_MASKED;

  return osc_bad_result(dwords_out[0]) ? ZX_ERR_INTERNAL : ZX_OK;
}

// Call the ACPI _PSV method to query the temperature OSPM will trigger
// a cooling policy.
zx_status_t acpi_psv_call(ACPI_HANDLE dev_obj, uint64_t* out) {
  ACPI_STATUS status = acpi_evaluate_integer(dev_obj, "_PSV", out);
  return acpi_to_zx_status(status);
}

// Call the ACPI _SEG method to query a PCI Host Bridge's segment group
zx_status_t acpi_seg_call(ACPI_HANDLE dev_obj, uint16_t* out_seg) {
  uint64_t out;
  ACPI_STATUS status = acpi_evaluate_integer(dev_obj, "_SEG", &out);
  // Lower 8 bits of _SEG returned integer is the PCI segment group.
  *out_seg = static_cast<uint8_t>(out & 0xFF);
  return acpi_to_zx_status(status);
}

// Call the ACPI _TMP method to query the temperaure of a thermal zone.
zx_status_t acpi_tmp_call(ACPI_HANDLE dev_obj, uint64_t* out) {
  ACPI_STATUS status = acpi_evaluate_integer(dev_obj, "_TMP", out);
  return acpi_to_zx_status(status);
}
