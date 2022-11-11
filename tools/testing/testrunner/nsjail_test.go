// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestBuild(t *testing.T) {
	cmdStart := []string{
		"/path/to/nsjail",
		"--disable_clone_newcgroup",
		"--quiet",
		"--rlimit_as", "soft",
		"--rlimit_fsize", "soft",
		"--rlimit_nofile", "soft",
		"--rlimit_nproc", "soft",
	}

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
			want: append(
				cmdStart,
				"--",
				"/foo/bar",
			),
		},
		{
			name: "Test disabling network isolation",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
			},
			subcmd: []string{"/foo/bar"},
			want: append(
				cmdStart,
				"--disable_clone_newnet",
				"--",
				"/foo/bar",
			),
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
					{
						Dst:      "/i/am/temporary",
						UseTmpfs: true,
					},
				},
			},
			subcmd: []string{"/foo/bar"},
			want: append(
				cmdStart,
				"--disable_clone_newnet",
				"--tmpfsmount", "/i/am/temporary",
				"--bindmount_ro", "/root/name:/jail/name",
				"--bindmount_ro", "/readonly:/readonly",
				"--bindmount", "/readwrite:/readwrite",
				"--",
				"/foo/bar",
			),
		},
		{
			name: "Test current working directory",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				Cwd: "/cwd",
			},
			subcmd: []string{"/foo/bar"},
			want: append(
				cmdStart,
				"--disable_clone_newnet",
				"--cwd", "/cwd",
				"--",
				"/foo/bar",
			),
		},
		{
			name: "Test environment variables",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				Env: map[string]string{
					"TEST": "test",
				},
			},
			subcmd: []string{"/foo/bar"},
			want: append(
				cmdStart,
				"--disable_clone_newnet",
				"--env", "TEST=test",
				"--",
				"/foo/bar",
			),
		},
		{
			name: "Test symlinks",
			cmdBuilder: &NsJailCmdBuilder{
				Bin: "/path/to/nsjail",
				Symlinks: map[string]string{
					"/bin/bash": "/bin/sh",
				},
			},
			subcmd: []string{"/foo/bar"},
			want: append(
				cmdStart,
				"--disable_clone_newnet",
				"--symlink", "/bin/bash:/bin/sh",
				"--",
				"/foo/bar",
			),
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
