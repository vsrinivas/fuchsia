// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

// Metrics is used for instrumentation
type Metrics struct {
	num_extensions_excluded      uint
	num_licensed                 uint
	num_non_single_license_files uint
	num_single_license_files     uint
	num_unlicensed               uint
	num_with_project_license     uint
}
