// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import "fmt"

// Metrics is used for instrumentation
type Metrics struct {
	values map[string]uint
	order  []string
}

func (metrics *Metrics) Init() {
	metrics.values = make(map[string]uint)
	metrics.order = []string{
		"num_extensions_excluded",
		"num_licensed",
		"num_matched_to_project_file",
		"num_non_single_license_files",
		"num_single_license_file_match",
		"num_single_license_files",
		"num_unlicensed",
		"num_with_project_license",
	}
	for _, key := range metrics.order {
		metrics.values[key] = 0
	}
}

func (metrics *Metrics) increment(key string) {
	if _, found := metrics.values[key]; !found {
		fmt.Printf("error: metric key (%s) not found\n", key)
	}
	metrics.values[key]++
}

func (metrics *Metrics) print() {
	fmt.Println("Metrics:")
	for _, value := range metrics.order {
		fmt.Printf("%s: %d\n", value, metrics.values[value])
	}
}
