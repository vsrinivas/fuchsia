// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ADDING A NEW PROTOCOL
// When adding a new protocol, add a macro call at the end of this file after
// the last protocol definition with a tag, value, name, and flags in the form:
//
// DDK_PROTOCOL_DEF(tag, value, protocol_name)
//
// The value must be a unique identifier that is just the previous protocol
// value plus 1.

// clang-format off

#ifndef DDK_FIDL_PROTOCOL_DEF
#error Internal use only. Do not include.
#else
DDK_FIDL_PROTOCOL_DEF(RPMB,           1, "fuchsia.rpmb.Rpmb")
#undef DDK_FIDL_PROTOCOL_DEF
#endif
