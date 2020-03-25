// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
	"golang.org/x/crypto/ssh"
)

type fakeRunner struct {
	reconnectIfNecessaryErrs  []error
	reconnectIfNecessaryCalls int
	runErrs                   []error
	runCalls                  int
	lastCmd                   []string
}

func (r *fakeRunner) Run(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	r.runCalls++
	r.lastCmd = command
	if r.runErrs == nil {
		return nil
	}
	err, remainingErrs := r.runErrs[0], r.runErrs[1:]
	r.runErrs = remainingErrs
	return err
}

func (r *fakeRunner) Close() error {
	return nil
}

func (r *fakeRunner) ReconnectIfNecessary(ctx context.Context) (*ssh.Client, error) {
	r.reconnectIfNecessaryCalls++
	if r.reconnectIfNecessaryErrs == nil {
		return nil, nil
	}
	err, remainingErrs := r.reconnectIfNecessaryErrs[0], r.reconnectIfNecessaryErrs[1:]
	r.reconnectIfNecessaryErrs = remainingErrs
	return nil, err
}

func TestSubprocessTester(t *testing.T) {
	cases := []struct {
		name    string
		test    build.Test
		runErrs []error
		wantErr bool
		wantCmd []string
	}{
		{
			name:    "no command no path",
			test:    build.Test{},
			wantErr: true,
		},
		{
			name: "no command yes path",
			test: build.Test{
				Path: "/foo/bar",
			},
			wantErr: false,
			wantCmd: []string{"/foo/bar"},
		},
		{
			name: "command succeeds",
			test: build.Test{
				Command: []string{"foo", "bar"},
			},
			wantErr: false,
			wantCmd: []string{"foo", "bar"},
		},
		{
			name: "command fails",
			test: build.Test{
				Command: []string{"foo", "bar"},
			},
			runErrs: []error{fmt.Errorf("test failed")},
			wantErr: true,
			wantCmd: []string{"foo", "bar"},
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeRunner{
				runErrs: c.runErrs,
			}
			tester := subprocessTester{
				r: runner,
			}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			_, err := tester.Test(context.Background(), c.test, ioutil.Discard, ioutil.Discard)
			if gotErr := (err != nil); gotErr != c.wantErr {
				t.Errorf("tester.Test got error: %v, want error: %t", err, c.wantErr)
			}
			if diff := cmp.Diff(c.wantCmd, runner.lastCmd); diff != "" {
				t.Errorf("Unexpected command run (-want +got):\n%s", diff)
			}
		})
	}
}

type fakeDataSinkCopier struct{}

func (*fakeDataSinkCopier) Copy(remoteDir, localDir string) (runtests.DataSinkMap, error) {
	return runtests.DataSinkMap{}, nil
}

func (*fakeDataSinkCopier) Close() error {
	return nil
}

func TestSSHTester(t *testing.T) {
	cases := []struct {
		name      string
		runErrs   []error
		reconErrs []error
		wantErr   bool
	}{
		{
			name:    "sucess",
			runErrs: []error{nil},
			wantErr: false,
		},
		{
			name: "test failure",
			// TODO(garymm,ihuh): Tests should not be getting run thrice.
			// Adding the test now to document current behavior and make the fix obviously correct.
			runErrs: []error{fmt.Errorf("test failed once"), fmt.Errorf("test failed twice"), fmt.Errorf("test failed thrice")},
			wantErr: true,
		},
		{
			name:      "connection error results in retry",
			runErrs:   []error{sshutil.ConnectionError, sshutil.ConnectionError, nil},
			reconErrs: []error{fmt.Errorf("failed to connect"), nil},
			wantErr:   false,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeRunner{
				reconnectIfNecessaryErrs: c.reconErrs,
				runErrs:                  c.runErrs,
			}
			tester := fuchsiaSSHTester{
				r:                           runner,
				copier:                      &fakeDataSinkCopier{},
				connectionErrorRetryBackoff: &retry.ZeroBackoff{},
			}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			wantReconnCalls := len(c.reconErrs)
			wantRunCalls := len(c.runErrs)
			_, err := tester.Test(context.Background(), build.Test{}, ioutil.Discard, ioutil.Discard)
			if gotErr := (err != nil); gotErr != c.wantErr {
				t.Errorf("tester.Test got error: %v, want error: %t", err, c.wantErr)
			}
			if wantReconnCalls != runner.reconnectIfNecessaryCalls {
				t.Errorf("ReconnectIfNecesssary() called wrong number of times. Gott: %d, Want: %d", runner.reconnectIfNecessaryCalls, wantReconnCalls)
			}
			if wantRunCalls != runner.runCalls {
				t.Errorf("Run() called wrong number of times. Got: %d, Want: %d", runner.runCalls, wantRunCalls)
			}
		})
	}
}

func TestSetCommand(t *testing.T) {
	cases := []struct {
		name        string
		test        build.Test
		useRuntests bool
		expected    []string
	}{
		{
			name:        "specified command is respected",
			useRuntests: false,
			test: build.Test{
				Path:    "/path/to/test",
				Command: []string{"a", "b", "c"},
			},
			expected: []string{"a", "b", "c"},
		},
		{
			name:        "use runtests URL",
			useRuntests: true,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cmx",
			},
			expected: []string{"runtests", "-t", "fuchsia-pkg://example.com/test.cmx", "-o", "REMOTE_DIR"},
		},
		{
			name:        "use runtests path",
			useRuntests: true,
			test: build.Test{
				Path: "/path/to/test",
			},
			expected: []string{"runtests", "-t", "test", "/path/to", "-o", "REMOTE_DIR"},
		},
		{
			name:        "system path",
			useRuntests: false,
			test: build.Test{
				Path: "/path/to/test",
			},
			expected: []string{"/path/to/test"},
		},
		{
			name:        "components v1",
			useRuntests: false,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cmx",
			},
			expected: []string{"run-test-component", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v2",
			useRuntests: false,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cm",
			},
			expected: []string{"run-test-suite", "fuchsia-pkg://example.com/test.cm"},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			setCommand(&c.test, c.useRuntests, "REMOTE_DIR")
			if !reflect.DeepEqual(c.test.Command, c.expected) {
				t.Errorf("unexpected command:\nexpected: %q\nactual: %q\n", c.expected, c.test.Command)
			}
		})

	}
}
