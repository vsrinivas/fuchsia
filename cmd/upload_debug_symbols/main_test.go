// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"io/ioutil"
	"os"
	"reflect"
	"sync"
	"testing"
)

type FakeGCSClient struct {
	uploaded map[string]string
	gcsFiles map[string]bool
	mu       sync.Mutex
}

func (client *FakeGCSClient) uploadSingleFile(ctx context.Context, name, localPath string) error {
	// Use a mutex since this function might run concurrently.
	client.mu.Lock()
	client.uploaded[name] = localPath
	client.mu.Unlock()
	return nil
}

func (client *FakeGCSClient) exists(object string) bool {
	return client.gcsFiles[object]
}

func TestRunCommand(t *testing.T) {
	tests := []struct {
		// The name of this test case.
		name string
		// The lines of the input ids.txt file
		input string
		// The set of files that already exist in GCS
		gcsFiles map[string]bool
		// The expected objects that should be written
		output map[string]string
	}{
		{
			name: "should upload files in idx.txt",
			input: "01634b09 /path/to/binaryA.elf\n" +
				"02298167 /path/to/binaryB\n" +
				"025abbbc /path/to/binaryC.so",
			output: map[string]string{
				"01634b09.debug": "/path/to/binaryA.elf",
				"02298167.debug": "/path/to/binaryB",
				"025abbbc.debug": "/path/to/binaryC.so",
			},
		},
		{
			name: "should not upload files already in cloud",
			input: "alreadyExistFile /path/to/binaryA.elf\n" +
				"01634b09 /path/to/binaryA.elf\n" +
				"02298167 /path/to/binaryB\n" +
				"025abbbc /path/to/binaryC.so",
			output: map[string]string{
				"01634b09.debug": "/path/to/binaryA.elf",
				"02298167.debug": "/path/to/binaryB",
				"025abbbc.debug": "/path/to/binaryC.so",
			},
			gcsFiles: map[string]bool{"alreadyExistFile.debug": true},
		},
		{
			name:   "should upload nothing if nothing in ids.txt",
			input:  "",
			output: map[string]string{},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Create the ids.txt file
			idsFile, err := ioutil.TempFile("", "ids.txt")
			if err != nil {
				t.Fatal(err)
			}
			defer os.Remove(idsFile.Name())
			idsFile.Write([]byte(tt.input))

			ctx := context.Background()
			client := FakeGCSClient{
				uploaded: make(map[string]string),
				gcsFiles: tt.gcsFiles,
			}
			uploadSymbolFiles(ctx, &client, idsFile.Name())

			want := tt.output
			got := client.uploaded
			if !reflect.DeepEqual(want, got) {
				t.Fatalf("wanted:\n%v\nbut got:\n%v\n", want, got)
			}
		})
	}
}
