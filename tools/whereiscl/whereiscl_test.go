// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseReviewURL(t *testing.T) {
	for _, url := range []string{
		"https://fxr/123456789",
		"https://fxr/123456789/some/file/path/foo.cc",
		"http://fxr/123456789",
		"http://fxr/123456789/some/file/path/foo.cc",
		"fxr/123456789",
		"fxr/123456789/some/file/path/foo.cc",
		"https://fuchsia-review.googlesource.com/c/fuchsia/+/123456789",
		"https://fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file/path/foo.cc",
		"http://fuchsia-review.googlesource.com/c/fuchsia/+/123456789",
		"http://fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file/path/foo.cc",
		"fuchsia-review.googlesource.com/c/fuchsia/+/123456789",
		"fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file/path/foo.cc",
	} {
		got, err := parseReviewURL(url)
		if err != nil {
			t.Fatalf("parseReviewURL: %v", err)
		}
		want := queryInfo{
			apiEndpoint: "https://fuchsia-review.googlesource.com",
			cl:          "123456789",
		}
		if d := cmp.Diff(want, got, cmp.AllowUnexported(want)); d != "" {
			t.Errorf("parseReviewURL: mismatch (-want +got):\n%s", d)
		}
	}
}

func TestParseReviewURL_invalidURL(t *testing.T) {
	for _, url := range []string{
		"https://fxr/non_digits",
		"https://random-url.com/foo/bar",
	} {
		_, err := parseReviewURL(url)
		if err == nil {
			t.Error("parseReviewURL: error expected; got nil")
		}
	}
}
