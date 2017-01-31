// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Shell controller protocol.
//
// To run shell with a controller, pass a controller channel handle as
// MX_HND_TYPE_USER1.
//
// Commands issued by the shell to the controller:
//
//  - "get_history" retrieves the shell history.
//    response payload: empty
//    response handles: a vmo where shell history is stored as '\n' separated
//      entries, including a trailing '\n' after the last entry.
//      The maximum length of a single history entry in the vmo including the
//      trailing '\n' is 1024 bytes.
//  - "add_to_history:<entry>" adds the given <entry> to the shell
//    history.

void controller_init();

void controller_add_to_history(const char*, size_t);
