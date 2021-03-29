// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

// Image represents an entry in an image manifest.
type Image struct {
	// Name is the canonical name of the image.
	Name string `json:"name"`

	// Path is the path to the image within the build directory.
	Path string `json:"path"`

	// Label is the GN label of the image.
	Label string `json:"label"`

	// Type is the shorthand for the type of the image (e.g., "zbi" or "blk").
	Type string `json:"type"`

	// PaveArgs is the list of associated arguments to pass to the bootserver
	// when paving.
	PaveArgs []string `json:"bootserver_pave,omitempty"`

	// PaveZedbootArgs is the list of associated arguments to pass to the bootserver
	// when paving zedboot
	PaveZedbootArgs []string `json:"bootserver_pave_zedboot,omitempty"`

	// NetbootArgs is the list of associated arguments to pass to the bootserver
	// when netbooting.
	NetbootArgs []string `json:"bootserver_netboot,omitempty"`
}

// ImageManifest is a JSON list of images produced by the Fuchsia build.
type ImageManifest = []Image
