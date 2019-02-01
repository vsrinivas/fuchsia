// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpointer

import (
	"sync"
	"testing"
)

func TestCPointer(t *testing.T) {
	p := New(t)
	t1, err := Value(p)
	if err != nil {
		t.Errorf("Value failed: %v", err)
	} else if t1 != t {
		t.Errorf("Value returned %v, expected %v", t1, t)
	}
	if err := Delete(p); err != nil {
		t.Errorf("Delete failed: %v", err)
	}
	if _, err := Value(p); err == nil {
		t.Errorf("Value succeeded unexpectedly")
	}
	if err := Delete(p); err == nil {
		t.Error("Delete succeeded unexpectedly after Delete")
	}
}

func TestConcurrent(t *testing.T) {
	const goroutines = 100
	const count = 100
	var wg sync.WaitGroup
	c := make(chan uintptr)
	for i := 0; i < goroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < count; i++ {
				c <- New(c)
			}
		}()
	}
	go func() {
		wg.Wait()
		close(c)
	}()
	m := make(map[uintptr]bool)
	for p := range c {
		if m[p] {
			t.Errorf("duplicate C pointer %d", p)
		}
		m[p] = true
		Delete(p)
	}
}
