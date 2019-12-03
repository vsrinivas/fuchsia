// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"os"
)

// Image represents an entry in an image manifest.
type Image struct {
	// Name is the canonical name of the image.
	Name string `json:"name"`

	// Path is the absolute path to the image.
	// Note: when unmarshalled from a manifest this entry actually gives the relative
	// location from the manifest's directory; we prepend that directory when loading. See
	// LoadImageModule() below.
	Path string `json:"path"`

	// Type is the shorthand for the type of the image (e.g., "zbi" or "blk").
	Type string `json:"type"`

	// PaveArgs is the list of associated arguments to pass to the bootserver
	// when paving.
	PaveArgs []string `json:"bootserver_pave"`

	// PaveZedbootArgs is the list of associated arguments to pass to the bootserver
	// when paving zedboot
	PaveZedbootArgs []string `json:"bootserver_pave_zedboot"`

	// NetbootArgs is the list of associated arguments to pass to the bootserver
	// when netbooting.
	NetbootArgs []string `json:"bootserver_netboot"`
}

// LoadImages reads in the entries indexed in the given image manifest.
func LoadImages(manifest string) ([]Image, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %v", manifest, err)
	}
	defer f.Close()
	var imgs []Image
	if err := json.NewDecoder(f).Decode(&imgs); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %v", manifest, err)
	}
	return imgs, nil
}
