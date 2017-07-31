// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Shell controller protocol.
//
// To run shell with a controller, pass a controller channel handle as
// PA_USER1.
//
// Messages sent by the shell to the controller:
//
//  - "get_history" retrieves the initial shell history record.
//    response payload: empty
//    response handles: a vmo where shell history is stored as '\n' separated
//      entries, including a trailing '\n' after the last entry.
//      The maximum length of a single history entry in the vmo including the
//      trailing '\n' is 1024 bytes.
//  - "add_local_entry:<entry>" adds the given <entry> to the history record
//
// Messages sent by the controller to the shell:
//
//  - "add_remote_entry:<entry>" informs the shell that a new entry has been
//    added to the history record from another client.

void controller_init();

void controller_add_local_entry(const char*, size_t);

void controller_pull_remote_entries();
