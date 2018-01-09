// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package merkle

import (
	"bytes"
	"fmt"
	"strings"
	"testing"
)

func makeFF(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = 0xff
	}
	return b
}

var fuchsia = []byte{0xFF, 0x00, 0x80}

func makeFuchsia() []byte {
	b := make([]byte, 0xFF0080)
	for i := 0; i < len(b); i += len(fuchsia) {
		copy(b[i:], fuchsia)
	}
	return b
}

var examples = []struct {
	Name   string
	Input  []byte
	Output string
}{
	{
		Name:   "empty",
		Input:  []byte{},
		Output: "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
	},
	{
		Name:   "oneblock",
		Input:  makeFF(8192),
		Output: "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737",
	},
	{
		Name:   "small",
		Input:  makeFF(65536),
		Output: "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf",
	},
	{
		Name:   "large",
		Input:  makeFF(2105344),
		Output: "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67",
	},
	{
		Name:   "unaligned",
		Input:  makeFF(2109440),
		Output: "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43",
	},
	{
		Name:   "fuchsia",
		Input:  makeFuchsia(),
		Output: "2feb488cffc976061998ac90ce7292241dfa86883c0edc279433b5c4370d0f30",
	},
}

func TestExamples(t *testing.T) {
	for _, ex := range examples {
		t.Run(ex.Name, func(t *testing.T) {
			var tree Tree
			_, err := tree.ReadFrom(bytes.NewReader(ex.Input))
			if err != nil {
				t.Fatal(err)
			}

			got := fmt.Sprintf("%x", tree.Root())
			want := ex.Output
			if strings.Compare(got, want) != 0 {
				t.Errorf("got %q, want %q", got, want)
			}
		})
	}
}
