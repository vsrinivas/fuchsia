// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"path/filepath"
	"strings"
)

// hasExt returns true if path has one of the extensions in the list.
func hasExt(path string, exts []string) bool {
	if ext := filepath.Ext(path); ext != "" {
		ext = ext[1:]
		for _, e := range exts {
			if e == ext {
				return true
			}
		}
	}
	return false
}

// hasLowerPrefix returns true if the name has one of files as a prefix in
// lower case.
func hasLowerPrefix(name string, files []string) bool {
	name = strings.ToLower(name)
	for _, f := range files {
		if strings.HasPrefix(name, f) {
			return true
		}
	}
	return false
}

// contains returns true if one of the strings in the string list (first parameter)
// matches the target string (second parameter)
func contains(list []string, target string) bool {
	for _, s := range list {
		if strings.Contains(target, s) {
			return true
		}
	}
	return false
}
