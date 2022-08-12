// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

// MetricsInterface defines a list of methods that each "metrics" struct
// in each check-licenses related package should implement. This result
// package uses this metrics interface to save the run results to disk
// after check-licenses finishes executing.
type MetricsInterface interface {
	// Counts returns a map of metric names (string) to integers.
	// Packages use this metric to count simple statistics
	// during the runthrough.
	Counts() map[string]int

	// Values returns a map of metric names (string) to string arrays.
	// Packages use this metric to keep track of a list of strings
	// (e.g. paths, license patterns, project names) during the runthrough.
	Values() map[string][]string

	// Files returns a map of metric names (string) to file data (byte slice).
	// Packages use this metric to save full files to disk at the end
	// of the runthrough.
	Files() map[string][]byte
}

type ResultMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
	files  map[string][]byte   `json:"files"`
}

const (
	NumInitTemplates  = "Number of initialized templates"
	NotFoundInFileMap = "Not Found in File Map"
)

var Metrics *ResultMetrics

func init() {
	Metrics = &ResultMetrics{
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

func (m *ResultMetrics) Counts() map[string]int {
	return m.counts
}

func (m *ResultMetrics) Values() map[string][]string {
	return m.values
}

func (m *ResultMetrics) Files() map[string][]byte {
	return m.files
}
