// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"reflect"
	"testing"
	"time"
)

func TestUnmarshalTestDurations(t *testing.T) {
	data := `[
		{
			"name": "/path/to/foo",
			"median_duration_ms": 12345
		},
		{
			"name": "/path/to/bar",
			"median_duration_ms": 99
		}
	]`
	expected := []TestDuration{
		{
			Name:           "/path/to/foo",
			MedianDuration: 12345 * time.Millisecond,
		},
		{
			Name:           "/path/to/bar",
			MedianDuration: 99 * time.Millisecond,
		},
	}
	var actual []TestDuration
	if err := json.Unmarshal([]byte(data), &actual); err != nil {
		t.Fatalf("error unmarshalling test durations: %v", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("got durations %#v, expected %#v", actual, expected)
	}
}
