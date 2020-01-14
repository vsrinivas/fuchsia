// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tarutil_test

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/tarutil"
)

func TestTarBytes(t *testing.T) {
	type entry struct {
		name, data string
	}

	tests := []struct {
		// A name for this test case.
		name string

		// The input tarball.
		input []entry

		// The expected contents of the archive written by WriteTo.
		output map[string]string
	}{
		{
			name:   "should handle an empty buffer",
			input:  []entry{{"", ""}},
			output: map[string]string{"": ""},
		},
		{
			name:  "should handle a non-empty buffer",
			input: []entry{{"a", string("a data")}},
			output: map[string]string{
				"a": string("a data"),
			},
		},
		{
			name: "should handle multiple non-empty buffers",
			input: []entry{
				{"a", string("a data")},
				{"b", string("b data")},
			},
			output: map[string]string{
				"a": string("a data"),
				"b": string("b data"),
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var buf bytes.Buffer
			tw := tar.NewWriter(&buf)
			for _, ent := range tt.input {
				tarutil.TarBytes(tw, []byte(ent.data), ent.name)
			}
			actual, err := readTar(&buf)
			if err != nil {
				t.Errorf("failed to read tar archive: %v", err)
				return
			}
			expected := tt.output
			if !reflect.DeepEqual(actual, expected) {
				t.Errorf("got:\n\n%v\n\nwanted:\n\n%v\n\n", actual, expected)
			}
		})
	}

}

// Helper function to read data from a gzipped tar archive. The output maps each header's
// name within the archive to its data.
func readTar(r io.Reader) (map[string]string, error) {
	tr := tar.NewReader(r)
	output := make(map[string]string)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break // End of archive.
		}
		if err != nil {
			return nil, fmt.Errorf("reading tarball failed, %v", err)

		}
		data := make([]byte, hdr.Size)
		if _, err := tr.Read(data); err != nil && err != io.EOF {
			return nil, fmt.Errorf("reading tarball data failed, %v", err)
		}
		output[hdr.Name] = string(data)
	}

	return output, nil
}
