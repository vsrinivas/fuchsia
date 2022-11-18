// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// License describes the output artifacts of //tools/check-licenses.
type License struct {
	// ComplianceFile points to the compliance output artifact
	ComplianceFile string `json:"compliance_file"`

	// LicenseFilesDir is the root directory where all license files reside
	LicenseFilesDir string `json:"license_files"`

	// RunFilesArchive is an archive of all the check-licenses output artifacts
	RunFilesArchive string `json:"run_files_archive"`
}
