// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

// Upload is a struct that contains source and destination paths to files to
// upload to GCS.
type Upload struct {
	// Source is the path to the local file to upload.
	Source string

	// Destination is the path to upload to relative to a gcs bucket and namespace.
	Destination string

	// Compress is a directive to gzip the object before uploading.
	Compress bool

	// Contents contains the contents to upload to Destination. This field
	// will only be used if Source is empty.
	Contents []byte

	// Deduplicate gives a collision strategy. If true, then an upload should
	// not fail in the event of a collision, allowing for deduplication of, for
	// example, content-addressed uploads.
	Deduplicate bool

	// Recursive tells whether to recursively upload all files in Source if
	// Source is a directory.
	Recursive bool

	// Metadata contains the metadata to be uploaded with the file.
	Metadata map[string]string
}
