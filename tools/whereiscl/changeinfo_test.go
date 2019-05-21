// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"net/http"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestGetChangeInfo(t *testing.T) {
	cl := "987654321"
	http.DefaultClient.Transport = &mockTransport{
		cl: cl,
		body: `)]}'
[
  {
    "foo": 42,
    "status": "MERGED",
    "current_revision": "abcdefg"
  }
]`,
	}
	got, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          cl,
	})
	if err != nil {
		t.Fatalf("getChangeInfo: %v", err)
	}
	want := &changeInfo{
		Status:          clStatusMerged,
		CurrentRevision: "abcdefg",
	}
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("getChangeInfo: mismatch (-want +got):\n%s", d)
	}
}

func TestGetChangeInfo_clNotFound(t *testing.T) {
	cl := "987654321"
	http.DefaultClient.Transport = &mockTransport{
		cl: cl,
		body: `)]}'
[]`,
	}
	_, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          cl,
	})
	if err == nil {
		t.Error("getChangeInfo: error expected; got nil")
	}
}

func TestGetChangeInfo_tooManyCLs(t *testing.T) {
	cl := "987654321"
	http.DefaultClient.Transport = &mockTransport{
		cl: cl,
		body: `)]}'
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
	}
	_, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          cl,
	})
	if err == nil {
		t.Error("getChangeInfo: error expected; got nil")
	}
}
