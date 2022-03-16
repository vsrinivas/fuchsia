// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

var AllFiles map[string]*File

func Initialize(c *FileConfig) error {
	AllFiles = make(map[string]*File, 0)

	Config = c
	return nil
}
