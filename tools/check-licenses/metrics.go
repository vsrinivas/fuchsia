// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"fmt"
	"sync"
)

// Metrics is used for instrumentation
type Metrics struct {
	values map[string]uint
	order  []string

	sync.RWMutex
}

func (metrics *Metrics) Init() {
	metrics.values = make(map[string]uint)
	metrics.order = []string{
		"num_extensions_excluded",
		"num_licensed",
		"num_non_single_license_files",
		"num_one_file_matched_to_multiple_single_licenses",
		"num_one_file_matched_to_one_single_license",
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

	metrics.Lock()
	if _, found := metrics.values[key]; !found {
		fmt.Printf("error: metric key (%s) not found\n", key)
	}
	metrics.values[key]++
	metrics.Unlock()
}

func (metrics *Metrics) print() {
	fmt.Println("Metrics:")
	for _, value := range metrics.order {
		fmt.Printf("%s: %d\n", value, metrics.values[value])
	}
}
