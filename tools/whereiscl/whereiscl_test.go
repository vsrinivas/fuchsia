// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io/ioutil"
	"log"
	"net/http"
	"strings"
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

// Fake http transport. This is used from other test files.
type mockTransport struct{ cl, body string }

func (t *mockTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	switch req.URL.Hostname() {
	case "fuchsia-review.googlesource.com":
		switch req.URL.Path {
		case "/changes/":
			cl := req.URL.Query().Get("q")
			if cl != t.cl {
				log.Fatalf("RoundTrip: invalid CL: %s", cl)
			}
		default:
			log.Fatalf("RoundTrip: invalid changes path: %s", req.URL.Path)
		}
	case "fuchsia.googlesource.com":
		manifestPrefix := "/integration/+/refs/heads/master/"
		if !strings.HasPrefix(req.URL.Path, manifestPrefix) {
			log.Fatalf("RoundTrip: invalid manifest path: %s", req.URL.Path)
		}
	default:
		log.Fatalf("RoundTrip: invalid hostname: %s", req.URL.Hostname())
	}

	resp := &http.Response{
		StatusCode: 200,
		Request:    req,
		Header:     http.Header{},
		Body:       ioutil.NopCloser(strings.NewReader(t.body)),
	}
	return resp, nil
}
