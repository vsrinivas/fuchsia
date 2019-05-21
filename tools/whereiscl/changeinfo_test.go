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
	transport := mockTransport{}
	transport.addResponse(
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
	got, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          "987654321",
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
	transport := mockTransport{}
	transport.addResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[]`,
	)
	http.DefaultClient.Transport = &transport
	_, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          "987654321",
	})
	if err == nil {
		t.Error("getChangeInfo: error expected; got nil")
	}
}

func TestGetChangeInfo_tooManyCLs(t *testing.T) {
	transport := mockTransport{}
	transport.addResponse(
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
	_, err := getChangeInfo(queryInfo{
		apiEndpoint: "https://fuchsia-review.googlesource.com",
		cl:          "987654321",
	})
	if err == nil {
		t.Error("getChangeInfo: error expected; got nil")
	}
}
