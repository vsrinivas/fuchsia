// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"bytes"
	"reflect"
	"testing"
)

func TestDecodeTestResultsFile(t *testing.T) {
	expected := &TestResultsFile{
		&TestCaseResults{
			Label:      "test case",
			TestSuite:  "test suite",
			Unit:       Nanoseconds,
			Values:     []float64{100, 200, 300},
			SplitFirst: true,
		},
	}

	input := []byte(`
        [
            {
                "label": "test case",
                "test_suite": "test suite",
                "unit": "nanoseconds",
                "values": [
                    100,
                    200,
                    300
                ],
                "split_first": true
            }
        ]`)

	output, err := DecodeTestResultsFile(input)

	if err != nil {
		t.Fatalf("failed to decode JSON: %s: %v", string(input), err)
	}

	if !reflect.DeepEqual(expected, output) {
		t.Fatalf("structs are different; expected:\n%#v\nbut got:\n%#v\n", expected, output)
	}
}

func TestTestResultsFile_Add(t *testing.T) {
	var file TestResultsFile

	results := &TestCaseResults{
		Label:      "test case",
		TestSuite:  "test suite",
		Unit:       Nanoseconds,
		Values:     []float64{100, 200, 300},
		SplitFirst: true,
	}
	file.Add(results)

	expected := TestResultsFile{results}
	if !reflect.DeepEqual(file, expected) {
		t.Errorf("expected \n%#v\n. Got \n%#v\n", expected, file)
	}
}

func TestTestResultsFile_Encode(t *testing.T) {
	file := &TestResultsFile{
		&TestCaseResults{
			Label:      "test case",
			TestSuite:  "test suite",
			Unit:       Nanoseconds,
			Values:     []float64{100, 200, 300},
			SplitFirst: true,
		},
	}

	encoded := new(bytes.Buffer)
	if err := file.Encode(encoded); err != nil {
		t.Fatalf("conversion to JSON failed: %v", err)
	}
	decoded, err := DecodeTestResultsFile(encoded.Bytes())
	if err != nil {
		t.Fatalf("decoding JSON failed: %v", err)
	}

	actual := decoded
	expected := file
	if !reflect.DeepEqual(actual, expected) {
		actualJSON := new(bytes.Buffer)
		if err := actual.Encode(actualJSON); err != nil {
			t.Fatal(err)
		}

		expectedJSON := new(bytes.Buffer)
		if err := expected.Encode(expectedJSON); err != nil {
			t.Fatal(err)
		}

		t.Fatalf("expected \n%s\nGot\n%s\n", expectedJSON, actualJSON)
	}
}
