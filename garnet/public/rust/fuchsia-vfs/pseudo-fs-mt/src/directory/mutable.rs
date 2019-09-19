// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Directories that live in this module are "mutalbe" from the client standpoint.  Their nested
//! entries can be modified by the clients, such as renamed, deleted, linked, or new entries can be
//! created.

pub mod entry_constructor;
