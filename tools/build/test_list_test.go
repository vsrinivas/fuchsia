// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestLoadTestList(t *testing.T) {
	manifest := `{
	  "schema_id": "experimental",
	  "data": [
	    {
	      "name": "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component1.cmx",
	      "labels": [
	        "//garnet/bin/fuchsia:fuchsia_pkg_test_component1(//build/toolchain/fuchsia:x64)"
	      ],
	      "tags": [
	        {
	          "key": "key",
	          "value": "value"
	        }
	      ]
	    },
	    {
	      "name": "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component2.cmx",
	      "labels": [
	        "//garnet/bin/fuchsia:fuchsia_pkg(//build/toolchain/fuchsia:x64)"
	      ],
	      "tags": [
	        {
	          "key": "key",
	          "value": "value"
	        },
	        {
	          "key": "key2",
	          "value": "value2"
	        }
	      ]
	    },
	    {
	      "name": "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component3.cmx",
	      "labels": [
	        "//garnet/bin/fuchsia:fuchsia_pkg(//build/toolchain/fuchsia:x64)"
	      ]
	    }
	  ]
	}`
	expected := map[string]TestListEntry{
		"fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component1.cmx": {
			Name:   "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component1.cmx",
			Labels: []string{"//garnet/bin/fuchsia:fuchsia_pkg_test_component1(//build/toolchain/fuchsia:x64)"},
			Tags:   []TestTag{{Key: "key", Value: "value"}},
		},
		"fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component2.cmx": {
			Name:   "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component2.cmx",
			Labels: []string{"//garnet/bin/fuchsia:fuchsia_pkg(//build/toolchain/fuchsia:x64)"},
			Tags:   []TestTag{{Key: "key", Value: "value"}, {Key: "key2", Value: "value2"}},
		},
		"fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component3.cmx": {
			Name:   "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component3.cmx",
			Labels: []string{"//garnet/bin/fuchsia:fuchsia_pkg(//build/toolchain/fuchsia:x64)"},
		},
	}
	testListPath := filepath.Join(t.TempDir(), "test-list.json")
	os.WriteFile(testListPath, []byte(manifest), os.ModePerm)
	testListEntries, err := LoadTestList(testListPath)
	if err != nil {
		t.Fatalf("error loading test list: %s", err)
	}
	if !reflect.DeepEqual(testListEntries, expected) {
		t.Fatalf("got test list: %#v\n\nexpected: %#v", testListEntries, expected)
	}
}

func TestLoadTestListVersionMismatch(t *testing.T) {
	manifest := `{
	  "schema_id": "1234",
	  "data": [
	    {
	      "name": "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component1.cmx",
	      "labels": [
	        "//garnet/bin/fuchsia:fuchsia_pkg_test_component1(//build/toolchain/fuchsia:x64)"
	      ],
	      "tags": [
	        {
	          "key": "key",
	          "value": "value"
	        }
	      ]
	    }
	  ]
	}`
	testListPath := filepath.Join(t.TempDir(), "test-list.json")
	os.WriteFile(testListPath, []byte(manifest), os.ModePerm)
	_, err := LoadTestList(testListPath)
	if err == nil {
		t.Fatalf("expected an error loading an unknown schema_id")
	}
	expected := `"schema_id" must be "experimental", found "1234"`
	if err.Error() != expected {
		t.Fatalf("got error '%s', expected '%s'", err, expected)
	}
}

func TestLoadTestListVersionMissing(t *testing.T) {
	manifest := `{
	  "data": [
	    {
	      "name": "fuchsia-pkg://fuchsia.com/fuchsia_pkg#meta/component1.cmx",
	      "labels": [
	        "//garnet/bin/fuchsia:fuchsia_pkg_test_component1(//build/toolchain/fuchsia:x64)"
	      ],
	      "tags": [
	        {
	          "key": "key",
	          "value": "value"
	        }
	      ]
	    }
	  ]
	}`
	testListPath := filepath.Join(t.TempDir(), "test-list.json")
	os.WriteFile(testListPath, []byte(manifest), os.ModePerm)
	_, err := LoadTestList(testListPath)
	if err == nil {
		t.Fatalf("expected an error loading a missing schema_id")
	}
	expected := `"schema_id" must be "experimental", found ""`
	if err.Error() != expected {
		t.Fatalf(`got error %q, expected %q`, err, expected)
	}
}
