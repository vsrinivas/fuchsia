// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bytes"
	"io"
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

func TestParseRequirements(t *testing.T) {
	expectedPkgs := map[string]string{
		"amber/0": "abcdef",
		"pkgfs/0": "123456789",
	}
	expectedImgs := map[string]struct{}{
		"dc38ffa1029c3fd44": {},
	}

	pFile, iFile := openDataSources()
	pkgs, imgs, err := ParseRequirements(pFile, iFile)

	if err != nil {
		t.Fatalf("Error processing requirements: %s", err)
	}

	for _, p := range pkgs {
		exp, ok := expectedPkgs[p.namever]
		if !ok {
			t.Fail()
			t.Logf("Package %s was found, but not expected", p)
			continue
		}
		if exp != p.merkle {
			t.Fail()
			t.Logf("Merkle does not match, expected %q, found %q", exp, p.merkle)
		}
		delete(expectedPkgs, p.namever)
	}

	if len(expectedPkgs) != 0 {
		for k, v := range expectedPkgs {
			t.Logf("Package [namever: %q, merkle: %q] was expected, but not found",
				k, v)
		}
		t.Fail()
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

func openDataSources() (io.ReadCloser, io.ReadCloser) {
	// fake stub
	return newByteReadCloser([]byte("amber/0=abcdef\npkgfs/0=123456789")),
		newByteReadCloser([]byte("dc38ffa1029c3fd44\n"))
}
