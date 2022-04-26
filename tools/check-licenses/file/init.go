// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

var AllFiles map[string]*File

func init() {
	AllFiles = make(map[string]*File, 0)
}

func Initialize(c *FileConfig) error {
	Config = c
	return nil
}
