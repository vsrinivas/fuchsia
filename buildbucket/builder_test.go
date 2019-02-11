// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildbucket

import (
	"flag"
	"reflect"
	"testing"

	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
)

func TestBuilderIDGetter(t *testing.T) {
	tests := []struct {
		// The name of this test case
		name string

		// The string to parse into a BuilderID
		input string

		// The expected result.
		output buildbucketpb.BuilderID

		// Whether to expect an error
		expectErr bool
	}{
		{
			name:  "should parse an input string into a BuilderID",
			input: "project/bucket/builder",
			output: buildbucketpb.BuilderID{
				Project: "project",
				Bucket:  "bucket",
				Builder: "builder",
			},
		}, {
			name:      "should err when the input contains < 2 fields",
			expectErr: true,
			output:    buildbucketpb.BuilderID{},
		}, {
			name:      "should err when the input is empty",
			expectErr: true,
			output:    buildbucketpb.BuilderID{},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			value := BuilderID()
			err := value.Set(tt.input)
			if err != nil != tt.expectErr {
				if tt.expectErr {
					t.Error("expected an err but got nil")
				} else {
					t.Errorf("wanted %v but got an err: %v", tt.output, err)
				}
			}

			// Compare flag values directly rather than using reflect.DeepEqual to prove
			// that `Get` returns a valid object.
			builderID := value.Get().(buildbucketpb.BuilderID)
			if !reflect.DeepEqual(builderID, tt.output) {
				t.Errorf("got\n%v\nbut wanted:\n%v", builderID, tt.output)
			}
		})
	}
}

func TestBuilderIDString(t *testing.T) {
	tests := []struct {
		// The name of this test case
		name string

		// The input BuilderID
		input flag.Value

		// The expected string.
		output string

		// Whether to expect an error
		expectErr bool
	}{
		{
			name: "should format the ID as a string",
			input: &builderIDFlag{
				Project: "project",
				Bucket:  "bucket",
				Builder: "builder",
			},
			output: "project/bucket/builder",
		}, {
			name: "when the ID is empty",
			input: &builderIDFlag{
				Project: "",
				Bucket:  "",
				Builder: "",
			},
			output: "//",
		}, {
			name: "when Project is empty",
			input: &builderIDFlag{
				Project: "",
				Bucket:  "bucket",
				Builder: "builder",
			},
			output: "/bucket/builder",
		}, {
			name: "when Bucket is empty",
			input: &builderIDFlag{
				Project: "project",
				Bucket:  "",
				Builder: "builder",
			},
			output: "project//builder",
		}, {
			name: "when Builder is empty",
			input: &builderIDFlag{
				Project: "project",
				Bucket:  "bucket",
				Builder: "",
			},
			output: "project/bucket/",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			output := tt.input.String()
			if output != tt.output {
				t.Errorf("got %q but wanted %q", output, tt.output)
			}
		})
	}
}
