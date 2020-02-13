// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_TESTS_DEVHOST_TEST_TEST_METADATA_H_
#define SRC_DEVICES_TESTS_DEVHOST_TEST_TEST_METADATA_H_

struct devhost_test_metadata {
  bool make_device_visible_success = true;
  bool init_reply_success = true;
};

#endif  // SRC_DEVICES_TESTS_DEVHOST_TEST_TEST_METADATA_H_
