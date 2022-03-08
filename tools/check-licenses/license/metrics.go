// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

type LicenseMetrics struct {
	counts map[string]int      `json:"counts"`
	values map[string][]string `json:"values"`
}

const (
	NumPatterns          = "Number of Unique License Patterns"
	UnrecognizedLicenses = "Number of Unrecognized License Texts"
	DuplicateLicenseText = "Number of Duplicate license texts"

	LicensePatternRestricted = "Patterns: Restricted Licenses"
	LicensePatternException  = "Patterns: Exception Licenses"
	LicensePatternNotice     = "Patterns: Notice Licenses"
)

var Metrics *LicenseMetrics

func init() {
	Metrics = &LicenseMetrics{
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

func (m *LicenseMetrics) Counts() map[string]int {
	return m.counts
}

func (m *LicenseMetrics) Values() map[string][]string {
	return m.values
}
