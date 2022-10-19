// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

func makeSDKManifest(t *testing.T, tools, cpus []string, manifestPath string) SDKManifest {
	t.Helper()
	manifest := SDKManifest{
		Atoms: []Atom{},
	}
	for _, tool := range tools {
		for _, cpu := range cpus {
			manifest.Atoms = append(manifest.Atoms, Atom{
				ID: fmt.Sprintf("sdk://tools/%s/%s", cpu, tool),
				Files: []File{
					{
						Source: fmt.Sprintf("%s/%s", cpu, tool),
					},
				},
			})
		}
	}
	if manifestPath != "" {
		if err := os.MkdirAll(filepath.Dir(manifestPath), os.ModePerm); err != nil {
			t.Fatalf("MkdirAll(%s) failed: %s", filepath.Dir(manifestPath), err)
		}
		if err := jsonutil.WriteToFile(manifestPath, manifest); err != nil {
			t.Fatalf("WriteToFile(%s, %v) failed: %s", manifestPath, manifest, err)
		}
	}
	return manifest
}

func TestAddFFXDeps(t *testing.T) {
	baseDeps := []string{
		"sdk/manifest/core",
		"product_bundle.json",
		"virtual_device_min.json",
		"virtual_device_recommended.json",
		"obj/build/images/flash/virtual_device_specification_recommended_flags.json.template",
	}
	testCases := []struct {
		name      string
		targetCPU string
		tools     []string
		want      []string
		wantTools []string
	}{
		{
			name:      "QEMU x64 deps with tools",
			targetCPU: "x64",
			tools:     []string{"zbi", "fvm", "qemu_internal"},
			want:      append(baseDeps, "physical_device.json", "x64/zbi", "x64/fvm", "x64/qemu_internal"),
			wantTools: []string{"zbi", "fvm", "qemu_internal"},
		},
		{
			name:      "AEMU x64 deps with tools",
			targetCPU: "x64",
			tools:     []string{"zbi", "fvm", "aemu_internal"},
			want:      append(baseDeps, "physical_device.json", "x64/zbi", "x64/fvm", "x64/aemu_internal"),
			wantTools: []string{"zbi", "fvm", "aemu_internal"},
		},
		{
			name:      "QEMU arm64 deps with tools",
			targetCPU: "arm64",
			tools:     []string{"zbi", "fvm", "qemu_internal"},
			want:      append(baseDeps, "arm64/zbi", "arm64/fvm", "arm64/qemu_internal"),
			wantTools: []string{"zbi", "fvm", "qemu_internal"},
		},
		{
			name:      "QEMU arm64 deps without emulator",
			targetCPU: "arm64",
			tools:     []string{"zbi", "fvm"},
			want:      append(baseDeps, "arm64/zbi", "arm64/fvm"),
			wantTools: []string{"zbi", "fvm"},
		},
		{
			name:      "deps without tools",
			targetCPU: "arm64",
			want:      baseDeps,
		},
		{
			name:      "QEMU arm64 deps with extra tools",
			targetCPU: "arm64",
			tools:     []string{"something"},
			want:      baseDeps,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			sdkRoot := t.TempDir()
			makeSDKManifest(
				t,
				[]string{"zbi", "fvm", "aemu_internal", "qemu_internal", "extra_tool"},
				[]string{"x64", "arm64"},
				filepath.Join(sdkRoot, "sdk", "manifest", "core"),
			)
			deps, err := GetEmuDeps(sdkRoot, tc.targetCPU, tc.tools)
			if err != nil {
				t.Errorf("failed to get ffx emu deps: %s", err)
			}
			sort.Strings(deps)
			sort.Strings(tc.want)
			if diff := cmp.Diff(tc.want, deps); diff != "" {
				t.Errorf("GetEmuDeps(%s, %s, %v) failed: (-want +got): \n%s", sdkRoot, tc.targetCPU, tc.tools, diff)
			}

			manifest, err := GetFFXEmuManifest(filepath.Join(sdkRoot, "sdk", "manifest", "core"), tc.targetCPU, tc.tools)
			if err != nil {
				t.Fatalf("failed to get ffx emu manifest: %s", err)
			}
			bytes, err := json.Marshal(manifest)
			if err != nil {
				t.Fatal(err)
			}
			var emuManifest SDKManifest
			if err := json.Unmarshal(bytes, &emuManifest); err != nil {
				t.Fatal(err)
			}
			expectedManifest := makeSDKManifest(t, tc.wantTools, []string{tc.targetCPU}, "")

			if diff := cmp.Diff(expectedManifest, emuManifest); diff != "" {
				t.Errorf("unexpected ffx emu manifest: (-want +got): \n%s", diff)
			}
		})
	}
}
