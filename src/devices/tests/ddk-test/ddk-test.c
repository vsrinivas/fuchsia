// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <unittest/unittest.h>

extern struct test_case_element* test_case_ddk_metadata;

zx_device_t* ddk_test_dev;

static void ddk_test_output_func(const char* line, int len, void* arg) {
  zx_handle_t h = *(zx_handle_t*)arg;
  // len is not actually the number of bytes to output
  zx_socket_write(h, 0u, line, strlen(line), NULL);
}

static void update_test_report(bool success, test_report_t* report) {
  report->n_tests++;
  if (success) {
    report->n_success++;
  } else {
    report->n_failed++;
  }
}

static zx_status_t ddk_test_func(void* cookie, test_report_t* report) {
  zx_device_t* dev = (zx_device_t*)cookie;

  test_protocol_t proto;
  zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_TEST, &proto);
  if (status != ZX_OK) {
    return status;
  }

  zx_handle_t output;
  proto.ops->get_output_socket(proto.ctx, &output);
  if (output != ZX_HANDLE_INVALID) {
    unittest_set_output_function(ddk_test_output_func, &output);
  }

  memset(report, 0, sizeof(*report));
  update_test_report(unittest_run_one_test(test_case_ddk_metadata, TEST_ALL), report);
  unittest_restore_output_function();
  zx_handle_close(output);
  return report->n_failed == 0 ? ZX_OK : ZX_ERR_INTERNAL;
}

static zx_device_t* child_dev = NULL;

static void child_unbind(void* ctx) { device_unbind_reply(child_dev); }

static zx_protocol_device_t child_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = child_unbind,
};

zx_status_t ddk_test_bind(void* ctx, zx_device_t* parent) {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "child",
      .ops = &child_device_ops,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };
  ZX_ASSERT(child_dev == NULL);
  zx_status_t status = device_add(parent, &args, &child_dev);
  if (status != ZX_OK) {
    return status;
  }

  test_protocol_t proto;
  status = device_get_protocol(parent, ZX_PROTOCOL_TEST, &proto);
  if (status != ZX_OK) {
    return status;
  }

  ddk_test_dev = parent;
  const test_func_t test = {ddk_test_func, parent};
  proto.ops->set_test_func(proto.ctx, &test);
  return ZX_OK;
}
