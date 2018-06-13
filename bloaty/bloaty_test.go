// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"fmt"
	"io/ioutil"
	"testing"
)

const (
	ids = `0071116f87e52dab0c21bd1cabc590fab5b06348 /out/libc.so
01885627175ee9996b08af6c64c95a6e30787269 /out/libpc-ps2.so
`
)

func WriteTempIdsTxt() (string, error) {
	idsPath, err := ioutil.TempFile("", "ids")
	if err != nil {
		return "", err
	}
	fmt.Fprintf(idsPath, ids)
	idsPath.Close()
	return idsPath.Name(), nil
}

func TestGetFiles(t *testing.T) {
	idsPath, errIds := WriteTempIdsTxt()
	if errIds != nil {
		t.Fatal(errIds)
	}

	actual, errConfig := getFiles(idsPath)
	if errConfig != nil {
		t.Fatal(errConfig)
	}

	expected := []string{"/out/libc.so", "/out/libpc-ps2.so"}

	if len(actual) != len(expected) {
		t.Fatalf("In TestGenConfig, expected \n%s but got \n%s", expected, actual)
	}

	for i, val := range expected {
		if actual[i] != val {
			t.Fatalf("In TestGenConfig, expected \n%s but got \n%s", expected, actual)
		}
	}
}
