// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package notice

import (
	"bytes"
	"flag"
	"fmt"
	"path/filepath"
	"testing"
)

var (
	testDataDir = flag.String("test_data_dir", "", "Path to test data directory")

	expectedData = []Data{
		{
			LibraryName: "First Library Name",
			LicenseText: []byte(`First license text
Lorum Ipsum Dolor`),
		},
		{
			LibraryName: "Second Library Name",
			LicenseText: []byte(`/* More License Text
 * All rights reserved.
 */`),
		},
	}
)

func TestFlutter(t *testing.T) {
	path := filepath.Join(*testDataDir, "example_flutter")
	d, err := ParseFlutter(path)
	if err != nil {
		t.Fatal(err)
	}
	checkResults(d, t)
}

func TestChromium(t *testing.T) {
	path := filepath.Join(*testDataDir, "example_chromium")
	d, err := ParseChromium(path)
	if err != nil {
		t.Fatal(err)
	}
	checkResults(d, t)

}
func TestGoogle(t *testing.T) {
	path := filepath.Join(*testDataDir, "example_google")
	d, err := ParseGoogle(path)
	if err != nil {
		t.Fatal(err)
	}
	checkResults(d, t)

}

func checkResults(d []*Data, t *testing.T) {
	if len(d) != 2 {
		for _, v := range d {
			fmt.Println(string(v.LicenseText))
		}
		t.Fatal(fmt.Errorf("Expected 2 data results, got %v \n%v\n", len(d), d))
	}

	if bytes.Equal(d[0].LicenseText, expectedData[0].LicenseText) {
		t.Fatal(fmt.Errorf("Expected first license text to be equal.\nWanted: %v\n\nGot: %v\n\n", expectedData[0].LicenseText, d[0].LicenseText))
	}

	if bytes.Equal(d[1].LicenseText, expectedData[1].LicenseText) {
		t.Fatal(fmt.Errorf("Expected second license text to be equal.\nWanted: %v\n\nGot: %v\n\n", expectedData[1].LicenseText, d[1].LicenseText))
	}
}
