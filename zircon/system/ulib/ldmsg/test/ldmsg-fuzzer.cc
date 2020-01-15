// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ldmsg/ldmsg.h>
#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider fuzzed_data(data, size);
  uint64_t ordinal = fuzzed_data.PickValueInArray({
    LDMSG_OP_DONE,
    LDMSG_OP_DONE_OLD,
    LDMSG_OP_CLONE,
    LDMSG_OP_CLONE_OLD,
    LDMSG_OP_LOAD_OBJECT,
    LDMSG_OP_CONFIG,
    LDMSG_OP_LOAD_OBJECT_OLD,
    LDMSG_OP_CONFIG_OLD,
  });

  size_t req_len;
  ldmsg_req_t req;
  req.header.ordinal = ordinal;
  auto remaining_data = fuzzed_data.ConsumeRemainingBytes<uint8_t>();
  zx_status_t status = ldmsg_req_encode(&req, &req_len, (const char*)remaining_data.data(),
                                        remaining_data.size());
  if (status != ZX_OK) {
    return 0;
  }

  const char* out;
  size_t len_out;
  status = ldmsg_req_decode(&req, req_len, &out, &len_out);
  return 0;
}
