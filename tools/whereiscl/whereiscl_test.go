// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
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

type reqURLRespBody struct {
	url  *url.URL
	body string
}

// Simplified URL equality test.
func equalURL(u1, u2 *url.URL) bool {
	if u1.Hostname() != u2.Hostname() {
		return false
	}
	if u1.EscapedPath() != u2.EscapedPath() {
		return false
	}
	if !cmp.Equal(u1.Query(), u2.Query()) {
		return false
	}
	return true
}

// Fake http transport. This is used from other test files.
type mockTransport struct {
	responses []reqURLRespBody
}

func (t *mockTransport) addResponse(urlStr, body string) {
	u, err := url.Parse(urlStr)
	if err != nil {
		log.Fatalf("mockTransport.addResponse: %v", err)
	}

	t.responses = append(t.responses, reqURLRespBody{
		url:  u,
		body: body,
	})
}

func (t *mockTransport) findResponse(u *url.URL) (string, bool) {
	for _, r := range t.responses {
		if equalURL(u, r.url) {
			return r.body, true
		}
	}
	return "", false
}

func (t *mockTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	body, ok := t.findResponse(req.URL)
	if !ok {
		log.Fatalf("RoundTrip: unrecognized URL: %v", req.URL.String())
	}
	resp := &http.Response{
		StatusCode: 200,
		Request:    req,
		Header:     http.Header{},
		Body:       ioutil.NopCloser(strings.NewReader(body)),
	}
	return resp, nil
}
