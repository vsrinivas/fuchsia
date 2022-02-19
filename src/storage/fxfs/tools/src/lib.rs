// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A set of helper functions for performing filesystem operations on images.
// These are both exposed as tools themselves and used as part of golden image generation.
pub mod ops;

// A set of helper functions for performing golden image generation and validation.
pub mod golden;
