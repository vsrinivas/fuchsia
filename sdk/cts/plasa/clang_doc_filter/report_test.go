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
						"name": "fuchsia::ui::composition::CreateImageArgs::ValueUnion_image_id::ValueUnion_image_id",
						"file": "fidling/gen/sdk/fidl/fuchsia.ui.composition/fuchsia.ui.composition/hlcpp/fuchsia/ui/composition/cpp/fidl.h",
						"line": 1675,
						"kind": "method"
					},
					{
						"name": "fuchsia::ui::composition::CreateImageArgs::ValueUnion_image_id::~ValueUnion_image_id",
						"file": "fidling/gen/sdk/fidl/fuchsia.ui.composition/fuchsia.ui.composition/hlcpp/fuchsia/ui/composition/cpp/fidl.h",
						"line": 1676,
						"kind": "method"
					}
		        ]}`,
		},
		{
			name: "fdio",
			args: args{
				allowlistNameRegexp: []string{`^fdio.*`},
			},
			expectedOutput: `{
			"items": [
				{"name": "fdio_bind_to_fd", "kind": "function"},
				{"name": "fdio_create", "kind": "function"},
				{"name": "fdio_cwd_clone", "kind": "function"},
				{"name": "fdio_fd_clone", "kind": "function"},
				{"name": "fdio_fd_create", "kind": "function"},
				{"name": "fdio_fd_create_null", "kind": "function"},
				{"name": "fdio_fd_transfer", "kind": "function"},
				{"name": "fdio_get_service_handle", "kind": "function"},
				{"name": "fdio_get_zxio", "kind": "function"},
				{"name": "fdio_ns_bind", "kind": "function"},
				{"name": "fdio_ns_bind_fd", "kind": "function"},
				{"name": "fdio_ns_chdir", "kind": "function"},
				{"name": "fdio_ns_connect", "kind": "function"},
				{"name": "fdio_ns_create", "kind": "function"},
				{"name": "fdio_ns_destroy", "kind": "function"},
				{"name": "fdio_ns_export", "kind": "function"},
				{"name": "fdio_ns_export_root", "kind": "function"},
				{"name": "fdio_ns_free_flat_ns", "kind": "function"},
				{"name": "fdio_ns_get_installed", "kind": "function"},
				{"name": "fdio_ns_is_bound", "kind": "function"},
				{"name": "fdio_ns_opendir", "kind": "function"},
				{"name": "fdio_ns_unbind", "kind": "function"},
				{"name": "fdio_null_create", "kind": "function"},
				{"name": "fdio_open", "kind": "function"},
				{"name": "fdio_open_at", "kind": "function"},
				{"name": "fdio_open_fd", "kind": "function"},
				{"name": "fdio_open_fd_at", "kind": "function"},
				{"name": "fdio_service_clone", "kind": "function"},
				{"name": "fdio_service_clone_to", "kind": "function"},
				{"name": "fdio_service_connect", "kind": "function"},
				{"name": "fdio_service_connect_at", "kind": "function"},
				{"name": "fdio_service_connect_by_name", "kind": "function"},
				{"name": "fdio_unbind_from_fd", "kind": "function"},
				{"name": "fdio_zxio_create", "kind": "function"}
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
