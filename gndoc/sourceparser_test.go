// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"strings"
	"testing"
)

func TestNewSourceMap(t *testing.T) {
	sources := `[
  {
    "name": "build",
    "path": "/path/to/fuchsia/build",
    "relativePath": "build",
    "remote": "https://fuchsia.googlesource.com/build",
    "revision": "43d2b55675d428d460fe6f91092bbf3c39552caf",
    "branches": [
      "mybranch",
      "master"
    ]
  },
  {
    "name": "buildtools",
    "path": "/path/to/fuchsia/buildtools",
    "relativePath": "buildtools",
    "remote": "https://fuchsia.googlesource.com/buildtools"
  }
]
`
	actual, err := NewSourceMap(strings.NewReader(sources))
	if err != nil {
		t.Fatalf("In TestNewSourceMap, unable to create source map: %s", err)
	}

	expected := SourceMap(make(map[string]string))
	expected["build"] = "https://fuchsia.googlesource.com/build/+/43d2b55675d428d460fe6f91092bbf3c39552caf"
	expected["buildtools"] = "https://fuchsia.googlesource.com/buildtools/+/master"

	if len(expected) != len(*actual) {
		t.Fatalf("In TestNewSourceMap, expected \n%d but got \n%d", len(expected), len(*actual))
	}

	if expected["build"] != (*actual)["build"] {
		t.Fatalf("In TestNewSourceMap, expected \n%s but got \n%s", expected["build"], (*actual)["build"])
	}

	if expected["buildtools"] != (*actual)["buildtools"] {
		t.Fatalf("In TestNewSourceMap, expected \n%s but got \n%s", expected["buildtools"], (*actual)["buildtools"])
	}
}

func TestGetSourceLink(t *testing.T) {
	sourceMap := SourceMap(make(map[string]string))
	sourceMap["build"] = "https://fuchsia.googlesource.com/build/+/43d2b55675d428d460fe6f91092bbf3c39552caf"
	sourceMap["buildtools"] = "https://fuchsia.googlesource.com/buildtools/+/master"

	expected := "https://fuchsia.googlesource.com/build/+/43d2b55675d428d460fe6f91092bbf3c39552caf/BUILD.gn#27"
	actual := sourceMap.GetSourceLink("//build/BUILD.gn", 27)
	if expected != actual {
		t.Fatalf("In TestNewSourceMap, expected \n%s but got \n%s", expected, actual)
	}

	expected = "https://fuchsia.googlesource.com/build/+/43d2b55675d428d460fe6f91092bbf3c39552caf/BUILD.gn"
	actual = sourceMap.GetSourceLink("//build/BUILD.gn", 0)
	if expected != actual {
		t.Fatalf("In TestNewSourceMap, expected \n%s but got \n%s", expected, actual)
	}

	expected = ""
	actual = sourceMap.GetSourceLink("//base/BUILD.gn", 0)
	if expected != actual {
		t.Fatalf("In TestNewSourceMap, expected \n%s but got \n%s", expected, actual)
	}
}
