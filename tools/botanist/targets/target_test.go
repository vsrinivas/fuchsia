// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"context"
	"reflect"
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

func TestDeriveTarget(t *testing.T) {
	ctx := context.Background()

	tests := []struct {
		name           string
		obj            string
		expectedTarget reflect.Type
	}{
		{
			name:           "derive aemu target",
			obj:            `{"type": "aemu", "target": "x64"}`,
			expectedTarget: reflect.TypeOf(&AEMUTarget{}),
		},
		{
			name:           "derive qemu target",
			obj:            `{"type": "qemu", "target": "arm64"}`,
			expectedTarget: reflect.TypeOf(&QEMUTarget{}),
		},
		// Testing DeriveTargets for "device" and "gce" is complex given that
		// the constructor functions for those two targets perform a good amount
		// of side effects, for example, creating a "gce" target will try to
		// initialize a gce instance.
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			actual, err := DeriveTarget(ctx, []byte(test.obj), Options{})
			if err != nil {
				t.Errorf("failed to derive target. err=%q", err)
			}
			if reflect.TypeOf(actual) != test.expectedTarget {
				t.Errorf("expected target type %q, got %q", test.expectedTarget, reflect.TypeOf(actual))
			}
		})
	}
}
