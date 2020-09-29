// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"io/ioutil"
	"path/filepath"
	"reflect"
	"testing"
)

type fileToWrite struct {
	name    string
	content string
}

// Verifies strings can be properly loaded from a json file.
func TestLoadStringsFromJson(t *testing.T) {
	for _, tc := range []struct {
		name            string
		file            fileToWrite
		pathToLoad      string
		expectedStrings []string
		wantError       bool
	}{
		{
			name: "success",
			file: fileToWrite{
				name: "file.json",
				content: `[
					"path/to/foo",
					"potato"
				]`,
			},
			pathToLoad:      "file.json",
			expectedStrings: []string{"path/to/foo", "potato"},
		},
		{
			name: "success no strings",
			file: fileToWrite{
				name:    "file.json",
				content: `[]`,
			},
			pathToLoad:      "file.json",
			expectedStrings: []string{},
		},
		{
			name: "failure json improperly formatted",
			file: fileToWrite{
				name: "file.json",
				content: `[
					Oops. This is not proper json.
				]`,
			},
			pathToLoad: "file.json",
			wantError:  true,
		},
		{
			name:       "failure json file does not exist",
			pathToLoad: "non_existent_file.json",
			wantError:  true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// Generate test env based on input.
			dirPath, err := ioutil.TempDir("", "env")
			if err != nil {
				t.Fatalf("Unable to create tempdir for loadBlobsFromPaths test: %v", err)
			}
			if tc.file.name != "" {
				if err := ioutil.WriteFile(filepath.Join(dirPath, tc.file.name), []byte(tc.file.content), 0666); err != nil {
					t.Fatalf("Unable to write file %s to tempdir: %v", tc.file.name, err)
				}
			}

			// Now that we're set up, we can actually load the strings.
			actualStrings, err := loadStringsFromJson(filepath.Join(dirPath, tc.pathToLoad))

			// Ensure the results match the expectations.
			if (err == nil) == tc.wantError {
				t.Fatalf("got error [%v], want error? %t", err, tc.wantError)
			}
			if err == nil && !reflect.DeepEqual(actualStrings, tc.expectedStrings) {
				t.Fatalf("got strings %#v, expected %#v", actualStrings, tc.expectedStrings)
			}
		})
	}

}
