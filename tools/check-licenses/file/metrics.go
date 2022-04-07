// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

type FileMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
}

const (
	RepeatedFileTraversal    = "Files that were access multiple times during traversal"
	NumFiles                 = "All Files"
	NumPotentialLicenseFiles = "Files that may have license information"
)

var Metrics *FileMetrics

func init() {
	Metrics = &FileMetrics{
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

func (m *FileMetrics) Counts() map[string]int {
	return m.counts
}

func (m *FileMetrics) Values() map[string][]string {
	return m.values
}
