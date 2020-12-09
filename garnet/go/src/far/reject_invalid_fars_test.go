// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package far

import (
	"os"
	"testing"
)

var (
	invalidTestFiles = []string{
		"invalid-magic-bytes.far",
		"index-entries-length-not-a-multiple-of-24-bytes.far",
		"directory-names-index-entry-before-directory-index-entry.far",
		"two-directory-index-entries.far",
		"two-directory-names-index-entries.far",
		"no-directory-index-entry.far",
		"no-directory-names-index-entry.far",
		"directory-chunk-length-not-a-multiple-of-32-bytes.far",
		"directory-chunk-not-tightly-packed.far",
		"path-data-offset-too-large.far",
		"path-data-length-too-large.far",
		"directory-entries-not-sorted.far",
		"directory-entries-with-same-name.far",
		"directory-names-chunk-length-not-a-multiple-of-8-bytes.far",
		"directory-names-chunk-not-tightly-packed.far",
		"directory-names-chunk-before-directory-chunk.far",
		"directory-names-chunk-overlaps-directory-chunk.far",
		"zero-length-name.far",
		"name-with-null-character.far",
		"name-with-leading-slash.far",
		"name-with-trailing-slash.far",
		"name-with-empty-segment.far",
		"name-with-dot-segment.far",
		"name-with-dot-dot-segment.far",
		"content-chunk-starts-early.far",
		"content-chunk-starts-late.far",
		"second-content-chunk-starts-early.far",
		"second-content-chunk-starts-late.far",
		"content-chunk-not-zero-padded.far",
		"content-chunk-overlap.far",
		"content-chunk-not-tightly-packed.far",
		"content-chunk-offset-past-end-of-file.far",
	}
)

func TestInvalidFars(t *testing.T) {
	for _, fn := range invalidTestFiles {
		f, err := os.Open("/pkg/data/" + fn)
		if err != nil {
			t.Errorf("cannot open test file: %v", err)
			continue
		}
		_, err = NewReader(f)
		if err == nil {
			t.Errorf("invalid archive %q passed validation", fn)
		}
	}
}
