// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bytes"
	"testing"
)

type byteReadCloser struct {
	*bytes.Reader
}

func newByteReadCloser(d []byte) *byteReadCloser {
	return &byteReadCloser{bytes.NewReader(d)}
}

func (b *byteReadCloser) Close() error {
	return nil
}

func TestParsePackagesLineFormatted(t *testing.T) {
	expectedPkgs := [2]string{
		"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
		"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789",
	}

	pFile := newByteReadCloser([]byte("amber/0=abcdef\npkgfs/0=123456789"))
	pkgs, err := ParsePackagesLineFormatted(pFile)
	if err != nil {
		t.Fatalf("Error processing packages: %s", err)
	}

	if len(expectedPkgs) != len(pkgs) {
		t.Logf("Length of parsed packages != expected")
		t.Fail()
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Fail()
			t.Logf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
		}
	}
}

func TestParsePackagesJson(t *testing.T) {
	expectedPkgs := [2]string{
		"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
		"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789",
	}

	pFile := newByteReadCloser([]byte(`
		{
			"version": 1,
			"content": [
				"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
				"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789"
				]
		}
	`))

	pkgs, err := ParsePackagesJson(pFile)
	if err != nil {
		t.Fatalf("Error processing packages: %s", err)
	}

	if len(expectedPkgs) != len(pkgs) {
		t.Logf("Length of parsed packages != expected")
		t.Fail()
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Fail()
			t.Logf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
		}
	}
}

func TestParseImages(t *testing.T) {
	expectedImgs := map[string]struct{}{
		"dc38ffa1029c3fd44": {},
	}

	iFile := newByteReadCloser([]byte("dc38ffa1029c3fd44\n"))
	imgs, err := ParseImages(iFile)
	if err != nil {
		t.Fatalf("Error processing images: %s", err)
	}

	for _, i := range imgs {
		if _, ok := expectedImgs[i]; !ok {
			t.Fail()
			t.Logf("Unexpected image %q found", i)
			continue
		}
		delete(expectedImgs, i)
	}

	if len(expectedImgs) > 0 {
		t.Fail()
		t.Logf("Some images were not found")
		for _, i := range expectedImgs {
			t.Logf("  %q expected, but not found", i)
		}
	}
}
