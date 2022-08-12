// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

type ProjectMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
	files  map[string][]byte   `json:"files"`
}

const (
	NumProjects                 = "Project Count"
	NumSkippedProjects          = "Projects Not Included In Notice Count"
	NumInitCustomProjects       = "Initialized Custom Project Count"
	NumPreviousProjectRetrieved = "Initialized Custom Project Retrieval Count"
	DuplicateReadmeFiles        = "Duplicate README.fuchsia Files"
	UnknownReadmeLines          = "Unexpected Line Found In README.fuchsia File"

	MissingName        = "Projects Missing Names"
	MissingLicenseFile = "Projects Missing License Files"
)

var Metrics *ProjectMetrics

func init() {
	Metrics = &ProjectMetrics{
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

func (m *ProjectMetrics) Counts() map[string]int {
	return m.counts
}

func (m *ProjectMetrics) Values() map[string][]string {
	return m.values
}

func (m *ProjectMetrics) Files() map[string][]byte {
	return m.files
}
