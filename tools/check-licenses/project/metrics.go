// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"fmt"
)

var (
	_counts map[string]int
	_values map[string][]string
)

const (
	NumProjects = "Project Count"

	MissingName        = "Projects Missing Names"
	MissingLicenseFile = "Projects Missing License Files"
)

func init() {
	_counts = map[string]int{}
	_values = map[string][]string{}
}

func plus1(key string) {
	_counts[key] = _counts[key] + 1
}

func plusVal(key string, val string) {
	_values[key] = append(_values[key], val)
}

func GetMetrics(indent string) string {
	metrics := ""
	for k, v := range _counts {
		metrics += fmt.Sprintf("%s%v: %v\n", indent, k, v)
	}
	for k, v := range _values {
		metrics += fmt.Sprintf("%s%v:\n", indent, k)
		for _, s := range v {
			metrics += fmt.Sprintf("%s  %v\n", indent, s)
		}
	}
	return metrics
}
