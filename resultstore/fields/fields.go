// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package fields defines constants for Result Store Upload API proto message fields that
// are referenced when calling UpdateXXXX methods on UploadClient.  Add constants to this
/// package as needed.
package fields

const (
	StartTime        = "timing.start_time"
	Duration         = "timing.duration"
	StatusAttributes = "status_attributes"
	Files            = "files"
)
