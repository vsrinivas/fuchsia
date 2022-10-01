// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"encoding/json"
)

var AllFiles map[string]*File

func init() {
	AllFiles = make(map[string]*File, 0)
}

func Initialize(c *FileConfig) error {
	// Save the config file to the out directory (if defined).
	if b, err := json.MarshalIndent(c, "", "  "); err != nil {
		return err
	} else {
		plusFile("_config.json", b)
	}

	Config = c
	return nil
}
