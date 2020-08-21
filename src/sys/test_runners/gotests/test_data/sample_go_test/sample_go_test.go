// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sample_go_test

import (
	"fmt"
	"testing"
)

func TestPassing(t *testing.T) {
	fmt.Println("This test will pass")
	fmt.Println("It will also print this line")
	fmt.Println("And this line")

}

func TestFailing(t *testing.T) {
	t.Errorf("This will fail")
}

func TestCrashing(t *testing.T) {
	panic("This will crash\n")
}

func TestSkipped(t *testing.T) {
	t.Skip("Skipping this test")
}

func TestSubtests(t *testing.T) {
	names := []string{"Subtest1", "Subtest2", "Subtest3"}
	for _, name := range names {
		t.Run(name, func(t *testing.T) {
		})
	}
}

func TestPrefix(t *testing.T) {
	fmt.Println("Testing that given two tests where one test is prefix of another can execute independently.")
}

func TestPrefixExtra(t *testing.T) {
	fmt.Println("Testing that given two tests where one test is prefix of another can execute independently.")
}

func TestPrintMultiline(t *testing.T) {
	fmt.Print("This test will ")
	fmt.Print("print the msg ")
	fmt.Println("in multi-line.")
}
