// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetype

import (
	"testing"
)

func TestTypeForFile(t *testing.T) {
	testCases := []struct {
		path             string
		expectedFileType FileType
	}{
		{
			path:             "foo/bar.cc",
			expectedFileType: CPP,
		},
		{
			path:             "foo/bar.invalid",
			expectedFileType: Unknown,
		},
		{
			path:             "bar/OWNERS",
			expectedFileType: Owners,
		},
	}

	for _, tc := range testCases {
		ft := TypeForFile(tc.path)
		if ft != tc.expectedFileType {
			t.Errorf("Wrong file type for %q: wanted %v, got %v", tc.path, tc.expectedFileType, ft)
		}
	}
}
