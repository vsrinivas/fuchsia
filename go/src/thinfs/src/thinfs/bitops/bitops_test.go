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

package bitops

import (
	"testing"
)

func TestFfs(t *testing.T) {
	// Defined in common_test.go.  Creates a new random number generator.
	r := setUp(t)

	for i := 0; i < 100; i++ {
		x := uint64(r.Int63())
		if x == 0 {
			// Just skip zeroes.
			continue
		}
		expected := ffs(x)
		if actual := FFS(x); actual != expected {
			t.Errorf("FFS(%#x) = %v; want %v\n", x, actual, expected)
		}
	}
}

func TestClz(t *testing.T) {
	// Defined in common_test.go.  Creates a new random number generator.
	r := setUp(t)

	for i := 0; i < 100; i++ {
		x := uint64(r.Int63())
		if x == 0 {
			// Just skip zeroes.
			continue
		}
		expected := clz(x)
		if actual := CLZ(x); actual != expected {
			t.Errorf("CLZ(%#x) = %v; want %v\n", x, actual, expected)
		}
	}
}
