// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"archive/tar"
)

// Upload is a struct that contains source and destination paths to files to
// upload to GCS.
type Upload struct {
	// Source is the path to the local file to upload.
	Source string `json:"source,omitempty"`

	// Destination is the path to upload to relative to a gcs bucket and namespace.
	Destination string `json:"destination"`

	// Compress is a directive to gzip the object before uploading.
	Compress bool `json:"compress,omitempty"`

	// Contents contains the contents to upload to Destination. This field
	// will only be used if Source is empty.
	Contents []byte `json:"contents,omitempty"`

	// Deduplicate gives a collision strategy. If true, then an upload should
	// not fail in the event of a collision, allowing for deduplication of, for
	// example, content-addressed uploads.
	Deduplicate bool `json:"deduplicate,omitempty"`

	// Recursive tells whether to recursively upload all files in Source if
	// Source is a directory.
	Recursive bool `json:"recursive,omitempty"`

	// Signed determines whether the object should be signed, provided that a
	// private key is provided.
	Signed bool `json:"signed,omitempty"`

	// TarHeader tells whether or not to compress with tar and contains the
	// associated header.
	TarHeader *tar.Header `json:"tar_header,omitempty"`
}
