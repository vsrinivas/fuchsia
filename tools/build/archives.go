// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type Archive struct {
	// Name is the canonical name of the image.
	Name string `json:"name"`

	// Path is the path to the archive within the build directory.
	Path string `json:"path"`

	// Type is the shorthand for the format of the archive (e.g. "tar" or "tgz").
	Type string `json:"type"`
}
