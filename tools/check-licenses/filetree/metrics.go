// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

type FileTreeMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
}

const (
	NumFiles    = "File Count"
	NumFolders  = "Folder Count"
	NumSymlinks = "Symlink Count"

	MissingProject = "Files Missing Projects"
)

var Metrics *FileTreeMetrics

func init() {
	Metrics = &FileTreeMetrics{
		counts: make(map[string]int),
		values: make(map[string][]string),
	}
}

func plus1(key string) {
	Metrics.counts[key] = Metrics.counts[key] + 1
}

func plusVal(key string, val string) {
	plus1(key)
	Metrics.values[key] = append(Metrics.values[key], val)
}

func (m *FileTreeMetrics) Counts() map[string]int {
	return m.counts
}

func (m *FileTreeMetrics) Values() map[string][]string {
	return m.values
}
