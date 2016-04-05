// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package handle

import (
	"sync"
	"testing"
)

func TestHandle(t *testing.T) {
	h := New(t)
	t1, err := Value(h)
	if err != nil {
		t.Errorf("Value failed: %v", err)
	} else if t1 != t {
		t.Errorf("Value returned %v, expected %v", t1, t)
	}
	if err := Delete(h); err != nil {
		t.Errorf("Delete failed: %v", err)
	}
	if _, err := Value(h); err == nil {
		t.Errorf("Value succeeded unexpectedly")
	}
	if err := Delete(h); err == nil {
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
	for h := range c {
		if m[h] {
			t.Errorf("duplicate handle %d", h)
		}
		m[h] = true
		Delete(h)
	}
}
