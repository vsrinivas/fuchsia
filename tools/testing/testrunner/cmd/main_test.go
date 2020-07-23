// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
)

const (
	testFunc         = "Test"
	copySinksFunc    = "CopySinks"
	runBugreportFunc = "RunBugreport"
)

type fakeTester struct {
	testErr   error
	runTest   func(testsharder.Test, io.Writer, io.Writer)
	funcCalls []string
}

func (t *fakeTester) Test(_ context.Context, test testsharder.Test, stdout, stderr io.Writer) (runtests.DataSinkReference, error) {
	t.funcCalls = append(t.funcCalls, testFunc)
	if t.runTest != nil {
		t.runTest(test, stdout, stderr)
	}
	return nil, t.testErr
}

func (t *fakeTester) Close() error {
	return nil
}

func (t *fakeTester) CopySinks(_ context.Context, _ []runtests.DataSinkReference) error {
	t.funcCalls = append(t.funcCalls, copySinksFunc)
	return nil
}

func (t *fakeTester) RunBugreport(_ context.Context, _ string) error {
	t.funcCalls = append(t.funcCalls, runBugreportFunc)
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
				Runs:        1,
				MaxAttempts: 1,
			},
			expectErr: true,
		}, {
			name: "missing OS",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					Path: "/foo/bar",
				},
				Runs:        1,
				MaxAttempts: 1,
			},
			expectErr: true,
		}, {
			name: "spurious package URL",
			test: testsharder.Test{
				Test: build.Test{
					Name:       "test1",
					OS:         "linux",
					PackageURL: "fuchsia-pkg://test1",
				},
				Runs:        1,
				MaxAttempts: 1,
			},
			expectErr: true,
		},
		{
			name: "missing required path",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "linux",
				},
				Runs:        1,
				MaxAttempts: 1,
			},
			expectErr: true,
		},
		{
			name: "missing required package_url or path",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "fuchsia",
				},
				Runs:        1,
				MaxAttempts: 1,
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
				MaxAttempts: 1,
			},
			expectErr: true,
		}, {
			name: "missing max attempts",
			test: testsharder.Test{
				Test: build.Test{
					Name: "test1",
					OS:   "linux",
					Path: "/foo/bar",
				},
				Runs: 1,
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
				Runs:        1,
				MaxAttempts: 1,
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
				Runs:        5,
				MaxAttempts: 1,
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
		maxAttempts    int
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
			testErr: fmt.Errorf("test failed"),
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
			testErr:        sshutil.ConnectionError{},
			expectedErr:    sshutil.ConnectionError{},
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
		}, {
			name: "retries test",
			test: build.Test{
				Name:       "fuchsia-pkg://foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			maxAttempts: 3,
			testErr:     fmt.Errorf("test failed"),
			expectedResult: []*testrunner.TestResult{{
				Name:   "fuchsia-pkg://foo/bar",
				Result: runtests.TestFailure,
			}},
		},
		{
			name: "returns on first success even if max attempts > 1",
			test: build.Test{
				Name:       "fuchsia-pkg://foo/bar",
				OS:         "fuchsia",
				PackageURL: "fuchsia-pkg://foo/bar",
			},
			maxAttempts: 5,
			expectedResult: []*testrunner.TestResult{{
				Name:   "fuchsia-pkg://foo/bar",
				Result: runtests.TestSuccess,
			}},
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
			if c.maxAttempts == 0 {
				c.maxAttempts = 1
			}
			for i := 0; i < c.runs; i++ {
				result, err := runTest(context.Background(), testsharder.Test{Test: c.test, Runs: c.runs, MaxAttempts: c.maxAttempts}, i, tester)

				if err != c.expectedErr {
					t.Errorf("got error: %v, expected: %v", err, c.expectedErr)
				}
				if err == nil {
					if !assertEqual(result, c.expectedResult[i]) {
						t.Errorf("got result: %v, expected: %v", result, c.expectedResult[i])
					}
				}
				if c.maxAttempts > 1 {
					funcCalls := strings.Join(tester.funcCalls, ",")
					testCount := strings.Count(funcCalls, testFunc)
					expectedTries := 1
					if c.testErr != nil {
						expectedTries = c.maxAttempts
					}
					if testCount != expectedTries {
						t.Errorf("ran test %d times, expected: %d", testCount, expectedTries)
					}
				}
			}
		})
	}
}
