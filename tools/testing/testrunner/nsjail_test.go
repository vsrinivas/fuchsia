// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestBuild(t *testing.T) {
	testCases := []struct {
		name       string
		cmdBuilder *NsJailCmdBuilder
		subcmd     []string
		want       []string
		wantErr    bool
	}{
		{
			name:       "Test that missing binary returns error",
			cmdBuilder: &NsJailCmdBuilder{},
			wantErr:    true,
		},
		{
			name: "Test that missing subcmd returns error",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
			},
			wantErr: true,
		},
		{
			name: "Test enabling network isolation",
			cmdBuilder: &NsJailCmdBuilder{
				Bin:            "/path/to/nsjail",
				IsolateNetwork: true,
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--",
				"/foo/bar",
			},
		},
		{
			name: "Test disabling network isolation",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--disable_clone_newnet",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--",
				"/foo/bar",
			},
		},
		{
			name: "Test mount points",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				MountPoints: []*MountPt{
					{
						Src: "/readonly",
					},
					{
						Src:      "/readwrite",
						Writable: true,
					},
					{
						Src:      "/root/name",
						Dst:      "/jail/name",
						Writable: false,
					},
				},
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--disable_clone_newnet",
				"--bindmount_ro", "/readonly:/readonly",
				"--bindmount", "/readwrite:/readwrite",
				"--bindmount_ro", "/root/name:/jail/name",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--",
				"/foo/bar",
			},
		},
		{
			name: "Test current working directory",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				Cwd: "/cwd",
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--disable_clone_newnet",
				"--cwd", "/cwd",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--",
				"/foo/bar",
			},
		},
		{
			name: "Test chroot",
			cmdBuilder: &NsJailCmdBuilder{
				Bin:    "/path/to/nsjail",
				Chroot: "/chroot",
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--disable_clone_newnet",
				"--bindmount", "/chroot:/",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--",
				"/foo/bar",
			},
		},
		{
			name: "Test environment variables",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				Env: []string{"TEST=test"},
			},
			subcmd: []string{"/foo/bar"},
			want: []string{
				"/path/to/nsjail",
				"--keep_env",
				"--disable_clone_newnet",
				"--rlimit_as", "soft",
				"--rlimit_fsize", "soft",
				"--rlimit_nofile", "soft",
				"--rlimit_nproc", "soft",
				"--env", "TEST=test",
				"--",
				"/foo/bar",
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := tc.cmdBuilder.Build(tc.subcmd)
			if err != nil && !tc.wantErr {
				t.Errorf("NsJailCmdBuilder.Build(%v) failed; got %s, want <nil> error", tc.subcmd, err)
			} else if err == nil && tc.wantErr {
				t.Errorf("NsJailCmdBuilder.Build(%v) succeeded unexpectedly", tc.subcmd)
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("NsJailCmdBuilder.Build(%v) returned unexpected command (-want +got):\n%s", tc.subcmd, diff)
			}
		})
	}
}
