// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"testing"
)

func TestMetricsInit(t *testing.T) {
	var metrics Metrics
	metrics.Init()
	num_values := len(metrics.values)
	num_order := len(metrics.order)
	want := 0
	if num_values == want {
		t.Errorf("%v(): got %v, want %v", t.Name(), num_values, want)
	}
	if num_values != num_order {
		t.Errorf("%v(): got %v, want %v", t.Name(), num_values, num_order)
	}
}
