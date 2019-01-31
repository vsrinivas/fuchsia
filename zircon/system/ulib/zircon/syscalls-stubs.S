// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Compatibility aliases

#define ALIAS(oldname, newname) \
.globl oldname ;\
.type oldname,STT_FUNC ;\
oldname = newname ;\

// Currently no aliases!
