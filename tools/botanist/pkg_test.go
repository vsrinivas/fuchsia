// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"testing"
)

func TestAddressLogic(t *testing.T) {
	t.Run("percentage signs are escaped", func(t *testing.T) {
		inputs := []string{
			"[fe80::a019:b0ff:fe21:64bd%qemu]:40860",
			"[fe80::a019:b0ff:fe21:64bd%25qemu]:40860",
		}
		expected := "[fe80::a019:b0ff:fe21:64bd%25qemu]:40860"
		for _, input := range inputs {
			actual := escapePercentSign(input)
			if actual != expected {
				t.Errorf("failed to escape percentage sign:\nactual: %s\nexpected: %s", actual, expected)
			}
		}
	})

	t.Run("derivation of the local-scoped local host", func(t *testing.T) {
		inputs := []string{
			"[fe80::a019:b0ff:fe21:64bd%qemu]:40860",
			"[fe80::a019:b0ff:fe21:64bd%25qemu]:40860",
		}
		expected := "[fe80::a019:b0ff:fe21:64bd%25qemu]"
		for _, input := range inputs {
			actual := localScopedLocalHost(input)
			if actual != expected {
				t.Errorf("failed to derive host:\nactual: %s\nexpected: %s", actual, expected)
			}
		}
	})
}
