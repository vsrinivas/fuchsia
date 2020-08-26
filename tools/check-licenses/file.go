// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import "strings"

func isSingleLicenseFile(name string, singleLicenseFiles []string) bool {
	file_name_lowered := strings.ToLower(name)
	for _, singleLicenseFile := range singleLicenseFiles {
		// example of file: LICENSE, LICENSE-THIRD-PARTY, ...
		if strings.Index(file_name_lowered, strings.ToLower(singleLicenseFile)) == 0 {
			return true
		}
	}
	return false
}
