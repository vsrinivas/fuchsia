// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"io/ioutil"
	"os"
	"reflect"
	"testing"
)

type FakeGCSClient struct {
	contensts map[string]string
}

func (client *FakeGCSClient) uploadSingleFile(ctx context.Context, url string, filePath string) error {
	client.contensts[url] = filePath
	return nil
}

func (client *FakeGCSClient) getObjects(ctx context.Context) (map[string]bool, error) {
	return map[string]bool{"alreadyExistFile.debug": true}, nil
}

func TestRunCommand(t *testing.T) {
	tests := []struct {
		// The name of this test case.
		name string
		// The lines of the input ids.txt file
		input string
		// The lines of the input ids.txt file
		hashes []string
		// The set of files referenced in input.
		files []string
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
			client := FakeGCSClient{make(map[string]string)}
			uploadSymbolFiles(ctx, &client, idsFile.Name())

			eq := reflect.DeepEqual(client.contensts, tt.output)
			if !eq {
				t.Fatal("The results are not the same", client.contensts, tt.output)
			}
		})
	}
}
