// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Compatibility aliases

#define ALIAS(oldname, newname) \
.globl oldname ;\
.type oldname,STT_FUNC ;\
oldname = newname ;\


// TODO: these will be removed once upstream code migrates away from
//       the legacy names

ALIAS(mx_handle_wait_one, mx_object_wait_one)
ALIAS(_mx_handle_wait_one, _mx_object_wait_one)

ALIAS(mx_handle_wait_many, mx_object_wait_many)
ALIAS(_mx_handle_wait_many, _mx_object_wait_many)

