// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package directory

type DirectoryMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
	files  map[string][]byte   `json:"files"`
}

const (
	NumFiles    = "File Count"
	NumFolders  = "Folder Count"
	NumSymlinks = "Symlink Count"

	FileMissingProject   = "Files Missing Projects"
	FolderMissingProject = "Folders Missing Projects"

	Skipped = "Skipped Items"
)

var Metrics *DirectoryMetrics

func init() {
	Metrics = &DirectoryMetrics{
		counts: make(map[string]int),
		values: make(map[string][]string),
		files:  make(map[string][]byte),
	}
}

func plus1(key string) {
	Metrics.counts[key] = Metrics.counts[key] + 1
}

func plusVal(key string, val string) {
	plus1(key)
	Metrics.values[key] = append(Metrics.values[key], val)
}

func plusFile(key string, content []byte) {
	Metrics.files[key] = content
}

func (m *DirectoryMetrics) Counts() map[string]int {
	return m.counts
}

func (m *DirectoryMetrics) Values() map[string][]string {
	return m.values
}

func (m *DirectoryMetrics) Files() map[string][]byte {
	return m.files
}
