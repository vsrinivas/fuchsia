// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"io/ioutil"
	"time"
)

// TestDuration encodes information about a test's running time.
// It implements the json.RawMessage interface for custom JSON decoding.
type TestDuration struct {
	Name           string `json:"name"`
	MedianDuration time.Duration
}

type testDurationAlias TestDuration

// A rawTestDuration represents a test duration entry as it appears "raw" in
// test_durations.json – i.e., with integer milliseconds for duration fields
// rather than time.Duration values.
//
// It uses testDurationAlias as a hack to prevent TestDuration.UnmarshalJSON from
// recursing infinitely, because the alias doesn't inherit the original type's
// UnmarshalJSON method.
type rawTestDuration struct {
	*testDurationAlias
	MedianDurationMS int64 `json:"median_duration_ms"`
}

func (d *TestDuration) MarshalJSON() ([]byte, error) {
	raw := &rawTestDuration{
		testDurationAlias: (*testDurationAlias)(d),
	}
	raw.MedianDurationMS = int64(d.MedianDuration / time.Millisecond)
	return json.Marshal(raw)
}

func (d *TestDuration) UnmarshalJSON(data []byte) error {
	raw := rawTestDuration{
		testDurationAlias: (*testDurationAlias)(d),
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	d.MedianDuration = time.Duration(raw.MedianDurationMS) * time.Millisecond
	return nil
}

// LoadTestDurations parses a file containing an array of JSON objects
// conforming to the TestDurationSchema, and returns a mapping of test name
// to TestDuration.
func LoadTestDurations(durationsFile string) ([]TestDuration, error) {
	bytes, err := ioutil.ReadFile(durationsFile)
	if err != nil {
		return nil, err
	}
	var durations []TestDuration
	if err := json.Unmarshal(bytes, &durations); err != nil {
		return nil, err
	}
	return durations, nil
}
