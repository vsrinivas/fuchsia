// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testhelper provides helper utilities for tests.
package testhelper

import (
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"strings"

	"github.com/google/go-cmp/cmp"
)

// MockTransport is a fake HTTP transport for tests.
type MockTransport struct {
	responses []reqURLRespBody
}

// AddResponse adds a fake response for the given URL.
func (t *MockTransport) AddResponse(urlStr, body string) {
	u, err := url.Parse(urlStr)
	if err != nil {
		log.Fatalf("MockTransport.addResponse: %v", err)
	}

	t.responses = append(t.responses, reqURLRespBody{
		url:  u,
		body: body,
	})
}

func (t *MockTransport) findResponse(u *url.URL) (string, bool) {
	for _, r := range t.responses {
		if equalURL(u, r.url) {
			return r.body, true
		}
	}
	return "", false
}

func (t *MockTransport) RoundTrip(req *http.Request) (*http.Response, error) {
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
