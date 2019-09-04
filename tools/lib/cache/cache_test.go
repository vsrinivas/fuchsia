// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package cache

import (
	"fmt"
	"testing"
)

func init() {
}

var testCases = []struct {
	key string
	val string
}{
	{"foo", "aaa"},
	{"bar", "bbb"},
	{"baz", "ccc"},
}

func TestAdd(t *testing.T) {
	var cache Cache = &LRUCache{}
	for _, tc := range testCases {
		t.Run(fmt.Sprintf("add %s", tc.key), func(t *testing.T) {
			cache.Add(tc.key, tc.val)
			if val, ok := cache.Get(tc.key); !ok {
				t.Fatalf("not found")
			} else if ok && val != tc.val {
				t.Fatalf("got %v; want %v", tc.val, val)
			}
		})
	}
}

func TestRemove(t *testing.T) {
	var cache Cache = &LRUCache{}
	for _, tc := range testCases {
		cache.Add(tc.key, tc.val)
	}
	for _, tc := range testCases {
		t.Run(fmt.Sprintf("remove %s", tc.key), func(t *testing.T) {
			cache.Remove(tc.key)
			if _, ok := cache.Get(tc.key); ok {
				t.Fatalf("not removed")
			}
		})
	}
}
