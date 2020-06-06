// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"io"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder/lib"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

type fakeTester struct {
	testErr error
	runTest func(testsharder.Test, io.Writer, io.Writer)
}

func (t *fakeTester) Test(_ context.Context, test testsharder.Test, stdout, stderr io.Writer) (runtests.DataSinkReference, error) {
	if t.runTest != nil {
		t.runTest(test, stdout, stderr)
	}
	return nil, t.testErr
}

func (t *fakeTester) Close() error {
	return nil
}

func (t *fakeTester) CopySinks(_ context.Context, _ []runtests.DataSinkReference) error {
	return nil
}

func assertEqual(t1, t2 *testrunner.TestResult) bool {
	return (t1.Name == t2.Name &&
		t1.Result == t2.Result &&
		t1.RunIndex == t2.RunIndex &&
		string(t1.Stdio) == string(t2.Stdio))
}

func TestValidateTest(t *testing.T) {
	cases := []struct {
		name      string
		test      testsharder.Test
		expectErr bool
	}{
		{
			name: "missing name",
			test: testsharder.Test{
				Test: build.Test{
					OS:   "linux",
					Path: "/foo/bar",
				},
				Runs: 1,
			},
			expectErr: true,
		}, {
			name: "missing OS",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					Path: "/foo/bar",
				},
				Runs: 1,
			},
			expectErr: true,
		}, {
			name: "missing required path",
			test: testsharder.Test{
				Test: build.Test{
					Name:       "test1",
					OS:         "linux",
					PackageURL: "fuchsia-pkg://test1",
				},
				Runs: 1,
			},
			expectErr: true,
		}, {
			name: "missing required packageurl",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "fuchsia",
					Path: "/foo/bar",
				},
				Runs: 1,
			},
			expectErr: true,
		}, {
			name: "missing runs",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "linux",
					Path: "/foo/bar",
				},
			},
			expectErr: true,
		}, {
			name: "valid test with path",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "linux",
					Path: "/foo/bar",
				},
				Runs: 1,
			},
			expectErr: false,
		}, {
			name: "valid test with packageurl",
			test: testsharder.Test{
				Test: build.Test{
					Name:       "test1",
					OS:         "fuchsia",
					PackageURL: "fuchsia-pkg://test1",
				},
				Runs: 5,
			},
			expectErr: false,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := validateTest(c.test)
			if c.expectErr != (err != nil) {
				t.Errorf("got error: %v, expectErr: %v", err, c.expectErr)
			}
		})
	}
}

func TestRunTest(t *testing.T) {
	cases := []struct {
		name           string
		test           build.Test
		runs           int
		testErr        error
		runTestFunc    func(testsharder.Test, io.Writer, io.Writer)
		expectedErr    error
		expectedResult []*testrunner.TestResult
	}{
		{
			name: "host test pass",
			test: build.Test{
				Name: "bar",
				Path: "/foo/bar",
				OS:   "linux",
			},
			testErr: nil,
			expectedResult: []*testrunner.TestResult{{
				Name:   "bar",
				Result: runtests.TestSuccess,
			}},
		},
		{
			name: "fuchsia test pass",
			test: build.Test{
				Name:       "bar",
				Path:       "/foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			testErr: nil,
			expectedResult: []*testrunner.TestResult{{
				Name:   "bar",
				Result: runtests.TestSuccess,
			}},
		},
		{
			name: "fuchsia test fail",
			test: build.Test{
				Name:       "bar",
				Path:       "/foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			testErr: errTestFailure{"test failed"},
			expectedResult: []*testrunner.TestResult{{
				Name:   "bar",
				Result: runtests.TestFailure,
			}},
		},
		{
			name: "fuchsia test ssh connection fail",
			test: build.Test{
				Name:       "bar",
				Path:       "/foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			testErr:        sshutil.ConnectionError,
			expectedErr:    sshutil.ConnectionError,
			expectedResult: nil,
		},
		{
			// Motivated by http://fxbug.dev/53349.
			name: "fuchsia test short write",
			test: build.Test{
				Name:       "bar",
				Path:       "/foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			testErr:        io.ErrShortWrite,
			expectedErr:    io.ErrShortWrite,
			expectedResult: nil,
		},
		{
			name: "multiplier test gets unique index",
			test: build.Test{
				Name:       "bar (2)",
				Path:       "/foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			runs:    2,
			testErr: nil,
			expectedResult: []*testrunner.TestResult{{
				Name:   "bar (2)",
				Result: runtests.TestSuccess,
			}, {
				Name:     "bar (2)",
				Result:   runtests.TestSuccess,
				RunIndex: 1,
			}},
		}, {
			name: "combines stdio and stdout in chronological order",
			test: build.Test{
				Name:       "fuchsia-pkg://foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			expectedResult: []*testrunner.TestResult{{
				Name:   "fuchsia-pkg://foo/bar",
				Result: runtests.TestSuccess,
				Stdio:  []byte("stdout stderr stdout"),
			}},
			runTestFunc: func(t testsharder.Test, stdout, stderr io.Writer) {
				stdout.Write([]byte("stdout "))
				stderr.Write([]byte("stderr "))
				stdout.Write([]byte("stdout"))
			},
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			tester := &fakeTester{
				testErr: c.testErr,
				runTest: c.runTestFunc,
			}
			if c.runs == 0 {
				c.runs = 1
			}
			for i := 0; i < c.runs; i++ {
				result, err := runTest(context.Background(), testsharder.Test{c.test, c.runs}, i, tester)

				if err != c.expectedErr {
					t.Errorf("got error: %v, expected: %v", err, c.expectedErr)
				}
				if err == nil {
					if !assertEqual(result, c.expectedResult[i]) {
						t.Errorf("got result: %v, expected: %v", result, c.expectedResult[i])
					}
				}
			}
		})
	}
}
