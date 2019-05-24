// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"net/http"
	"testing"

	th "fuchsia.googlesource.com/fuchsia/tools/whereiscl/testhelper"
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
		"123456789",
	} {
		got, err := ParseReviewURL(url)
		if err != nil {
			t.Fatalf("ParseReviewURL(%q): %v", url, err)
		}
		want := QueryInfo{
			APIEndpoint: "https://fuchsia-review.googlesource.com",
			CL:          "123456789",
		}
		if d := cmp.Diff(want, got, cmp.AllowUnexported(want)); d != "" {
			t.Errorf("ParseReviewURL(%q): mismatch (-want +got):\n%s", url, d)
		}
	}
}

func TestParseReviewURL_ChangeId(t *testing.T) {
	for _, url := range []string{
		"https://fxr/Ie8dddbce1eeb01a561f3b36e1685f4136fb61378",
		"fxr/Ie8dddbce1eeb01a561f3b36e1685f4136fb61378",
		"Ie8dddbce1eeb01a561f3b36e1685f4136fb61378",
	} {
		got, err := ParseReviewURL(url)
		if err != nil {
			t.Fatalf("ParseReviewURL(%q): %v", url, err)
		}
		want := QueryInfo{
			APIEndpoint: "https://fuchsia-review.googlesource.com",
			CL:          "Ie8dddbce1eeb01a561f3b36e1685f4136fb61378",
		}
		if d := cmp.Diff(want, got, cmp.AllowUnexported(want)); d != "" {
			t.Errorf("ParseReviewURL(%q): mismatch (-want +got):\n%s", url, d)
		}
	}
}

func TestParseReviewURL_invalidURL(t *testing.T) {
	for _, url := range []string{
		"https://fxr/non_digits/foo/bar",
		"https://random-url.com/foo/bar",
	} {
		qi, err := ParseReviewURL(url)
		if err == nil {
			t.Errorf("ParseReviewURL(%q): error expected; got nil with result %+v", url, qi)
		}
	}
}

func TestGetChangeInfo(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[
  {
    "foo": 42,
    "status": "MERGED",
    "current_revision": "abcdefg"
  }
]`,
	)

	http.DefaultClient.Transport = &transport
	got, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err != nil {
		t.Fatalf("GetChangeInfo: %v", err)
	}
	want := &ChangeInfo{
		Status:          CLStatusMerged,
		CurrentRevision: "abcdefg",
	}
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("GetChangeInfo: mismatch (-want +got):\n%s", d)
	}
}

func TestGetChangeInfo_clNotFound(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[]`,
	)
	http.DefaultClient.Transport = &transport
	_, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err == nil {
		t.Error("GetChangeInfo: error expected; got nil")
	}
}

func TestGetChangeInfo_tooManyCLs(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[
  {
    "status": "ACTIVE",
    "current_revision": "abcdefg"
  },
  {
    "status": "MERGED",
    "current_revision": "hijklmn"
  }
]`,
	)
	http.DefaultClient.Transport = &transport
	_, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err == nil {
		t.Error("GetChangeInfo: error expected; got nil")
	}
}
