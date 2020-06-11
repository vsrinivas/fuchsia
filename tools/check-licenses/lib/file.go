// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import "strings"

func isSingleLicenseFile(name string, singleLicenseFiles []string) bool {
	for _, singleLicenseFile := range singleLicenseFiles {
		// example of file: LICENSE, LICENSE-THIRD-PARTY, ...
		if strings.Index(name, singleLicenseFile) == 0 {
			return true
		}
	}
	return false
}
