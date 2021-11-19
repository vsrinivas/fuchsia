// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

package main

import (
	"flag"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/sdk/cts/plasa/model"
)

var testDir = flag.String("test_data_dir", "", "The directory where test data reside")

func TestTestDir(t *testing.T) {
	if *testDir == "" {
		t.Fatalf("the required flag --test_data_dir=... was not supplied")
	}
}

func TestReportGeneration(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name           string
		args           args
		expectedOutput string
	}{
		{
			name: "fidling",
			args: args{
				allowlistFilenameRegexp: []string{"fidling"},
			},
			expectedOutput: `{
                "items": [
                    {
                            "name":       "fuchsia::ui::composition::CreateImageArgs::ValueUnion_image_id::ValueUnion_image_id",
                            "file":   "fidling/gen/sdk/fidl/fuchsia.ui.composition/fuchsia.ui.composition/hlcpp/fuchsia/ui/composition/cpp/fidl.h",
                            "line": 1675,
                            "kind":       "method",
                            "return_type": "void",
                            "params":     "()"
                    },
                    {
                            "name":       "fuchsia::ui::composition::CreateImageArgs::ValueUnion_image_id::~ValueUnion_image_id",
                            "file":   "fidling/gen/sdk/fidl/fuchsia.ui.composition/fuchsia.ui.composition/hlcpp/fuchsia/ui/composition/cpp/fidl.h",
                            "line": 1676,
                            "kind":       "method",
                            "return_type": "void",
                            "params":     "()"
                    }
            ]}`,
		},
		{
			name: "fdio_open",
			args: args{
				allowlistNameRegexp: []string{`^fdio_open.*`},
			},
			expectedOutput: `{
            "items": [
                {
                        "name":       "fdio_open",
                        "kind":       "function",
                        "return_type": "zx_status_t",
                        "params":     "(const char * path,uint32_t flags,zx_handle_t request)"
                },
                {
                        "name":       "fdio_open_at",
                        "kind":       "function",
                        "return_type": "zx_status_t",
                        "params":     "(zx_handle_t directory,const char * path,uint32_t flags,zx_handle_t request)"
                },
                {
                        "name":       "fdio_open_fd",
                        "kind":       "function",
                        "return_type": "zx_status_t",
                        "params":     "(const char * path,uint32_t flags,int * out_fd)"
                },
                {
                        "name":       "fdio_open_fd_at",
                        "kind":       "function",
                        "return_type": "zx_status_t",
                        "params":     "(int dir_fd,const char * path,uint32_t flags,int * out_fd)"
                }
            ]}
            `,
		},
		{
			name: "enum report",
			args: args{
				allowlistNameRegexp: []string{`^fidl_trigger_tag$`},
			},
			expectedOutput: `{
            "items": [
               {
                    "name": "fidl_trigger_tag",
                    "kind": "enum"
               }
            ]}
            `,
		},
		{
			name: "enum member report",
			args: args{
				allowlistNameRegexp: []string{`^fidl_trigger_tag::kFidlTriggerWillCChannelRead$`},
			},
			expectedOutput: `{
            "items": [
               {
                    "name": "fidl_trigger_tag::kFidlTriggerWillCChannelRead",
                    "kind": "enum/member"
               }
            ]}
            `,
		},
		{
			name: "method report",
			args: args{
				allowlistNameRegexp: []string{`^zx::channel::write$`},
			},
			expectedOutput: `{
            "items": [
               {
                    "name": "zx::channel::write",
                    "kind": "method",
                    "file": "../../zircon/system/ulib/zx/include/lib/zx/channel.h",
                    "line": 46,
                    "return_type": "zx_status_t",
                    "params": "(uint32_t flags,const void * bytes,uint32_t num_bytes,const zx_handle_t * handles,uint32_t num_handles)"
               }
            ]}
            `,
		},
		{
			name: "anonymous report",
			args: args{
				allowlistNameRegexp: []string{`^\(anonymous\)$`},
			},
			expectedOutput: `{
            "items": [
               {
                   "name": "(anonymous)",
                   "kind": "record"
               },
               {
                   "name": "(anonymous)",
                   "kind": "record"
               }
            ]}
            `,
		},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			dirName := *testDir
			var output strings.Builder
			if err := run(dirName, &output, test.args); err != nil {
				t.Fatalf("error invoking main.run() from the test:\n\t%v", err)
			}

			actual, err := model.ReadReportJSON(strings.NewReader(output.String()))
			if err != nil {
				t.Fatalf("error: could not read report from JSON:\n\t%v", err)
			}

			expected, err := model.ReadReportJSON(strings.NewReader(test.expectedOutput))
			if err != nil {
				t.Fatalf("error: could not read from expectedOutput:\n\t%v", err)
			}
			if !cmp.Equal(expected, actual, cmpopts.IgnoreUnexported(model.Report{})) {
				t.Errorf("error:\n\twant: %+v\n\ngot: %+v\n\tdiff: %v",
					expected, actual, cmp.Diff(actual, expected, cmpopts.IgnoreUnexported(model.Report{})))
			}
		})
	}
}
