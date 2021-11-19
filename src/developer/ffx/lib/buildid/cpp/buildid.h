// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FFX_LIB_BUILDID_CPP_BUILDID_H_
#define SRC_DEVELOPER_FFX_LIB_BUILDID_CPP_BUILDID_H_

extern "C" {

// get_build_id reads the current process build-id from process memory, and
// copies it into out. On success, the return value specifies the length of the
// buildid returned, on failure a value less than one is returned, and the value
// of out is undefined.
int get_build_id(char out[32]);
}

#endif
