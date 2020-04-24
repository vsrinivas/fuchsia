// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"reflect"
	"testing"
)

func testCase(t *testing.T, stdout string, want []TestCaseResult) {
	parsed := Parse([]byte(stdout))
	if !reflect.DeepEqual(parsed, want) {
		t.Errorf("Parse(stdout) = %v; want %v", parsed, want)
	}
}

func testData(name string, status TestCaseStatus, duration string) TestCaseResult {
	return makeTestCaseResult([]byte(name), status, []byte(duration))
}

func TestMakeTestCaseResult(t *testing.T) {
	name := "TestSuiteName.TestCaseName"
	actual := testData(name, Pass, "4ms")
	if actual.Name != name {
		t.Errorf("Name = %v; want %v", actual.Name, name)
	}
	if actual.Status != Pass {
		t.Errorf("Status = %v; want Pass", actual.Status)
	}
	if actual.Duration.Milliseconds() != 4 {
		t.Errorf("actual.Duration = %v; want 4 millis", actual.Duration)
	}
}

func TestParseEmpty(t *testing.T) {
	testCase(t, "", []TestCaseResult{})
}

func TestParseInvalid(t *testing.T) {
	stdout := `
Mary had a little lamb
Its fleece was white as snow
And everywhere that Mary went
The lamb was sure to go
`
	testCase(t, stdout, []TestCaseResult{})
}

func TestParseGoogleTest(t *testing.T) {
	stdout := `
Some times there is weird stuff in stdout.
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from SynonymDictTest
[ RUN      ] SynonymDictTest.IsInitializedEmpty
[       OK ] SynonymDictTest.IsInitializedEmpty (4 ms)
[ RUN      ] SynonymDictTest.ReadingEmptyFileReturnsFalse
[       OK ] SynonymDictTest.ReadingEmptyFileReturnsFalse (3 ms)
[ RUN      ] SynonymDictTest.ReadingNonexistentFileReturnsFalse
Some times tests print to stdout.
Their prints get interleaved with the results.
[       OK ] SynonymDictTest.ReadingNonexistentFileReturnsFalse (4 ms)
[ RUN      ] SynonymDictTest.LoadDictionary
[       OK ] SynonymDictTest.LoadDictionary (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsReturnsListOfWords
[       OK ] SynonymDictTest.GetSynonymsReturnsListOfWords (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable
[       OK ] SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable (4 ms)
[ RUN      ] SynonymDictTest.AllWordsAreSynonymsOfEachOther
[       OK ] SynonymDictTest.AllWordsAreSynonymsOfEachOther (4 ms)
[ RUN      ] SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs
[  FAILED  ] SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs (4 ms)
[ RUN      ] SynonymDictTest.CompoundWordBug
[  SKIPPED ] SynonymDictTest.CompoundWordBug (4 ms)
[----------] 9 tests from SynonymDictTest (36 ms total)
[----------] Global test environment tear-down
[==========] 9 tests from 1 test suite ran. (38 ms total)
[  PASSED  ] 9 tests.
`
	testCase(t, stdout, []TestCaseResult{
		testData("SynonymDictTest.IsInitializedEmpty", Pass, "4ms"),
		testData("SynonymDictTest.ReadingEmptyFileReturnsFalse", Pass, "3ms"),
		testData("SynonymDictTest.ReadingNonexistentFileReturnsFalse", Pass, "4ms"),
		testData("SynonymDictTest.LoadDictionary", Pass, "4ms"),
		testData("SynonymDictTest.GetSynonymsReturnsListOfWords", Pass, "4ms"),
		testData("SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable", Pass, "4ms"),
		testData("SynonymDictTest.AllWordsAreSynonymsOfEachOther", Pass, "4ms"),
		testData("SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs", Fail, "4ms"),
		testData("SynonymDictTest.CompoundWordBug", Skip, "4ms"),
	})
}

func TestParseGo(t *testing.T) {
	stdout := `
==================== Test output for //experimental/users/shayba/testparser:test:
=== RUN   TestParseEmpty
--- PASS: TestParseEmpty (0.01s)
=== RUN   TestParseInvalid
--- PASS: TestParseInvalid (0.02s)
=== RUN   TestParseGoogleTest
    TestParseGoogleTest: experimental/users/shayba/testparser/testparser_test.go:15: Parse(invalid).Parse() = [{SynonymDictTest.IsInitializedEmpty Pass 4} {SynonymDictTest.ReadingEmptyFileReturnsFalse Pass 3} {SynonymDictTest.ReadingNonexistentFileReturnsFalse Pass 4} {SynonymDictTest.LoadDictionary Pass 4} {SynonymDictTest.GetSynonymsReturnsListOfWords Pass 4} {SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable Pass 4} {SynonymDictTest.AllWordsAreSynonymsOfEachOther Pass 4} {SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs Fail 4} {SynonymDictTest.CompoundWordBug Skip 4}]; want []
--- FAIL: TestParseGoogleTest (3.00s)
=== RUN   TestFail
    TestFail: experimental/users/shayba/testparser/testparser_test.go:68: Oops!
--- FAIL: TestFail (0.00s)
=== RUN   TestSkip
    TestSkip: experimental/users/shayba/testparser/testparser_test.go:72: Huh?
--- SKIP: TestSkip (0.00s)
FAIL
`
	testCase(t, stdout, []TestCaseResult{
		testData("TestParseEmpty", Pass, "0.01s"),
		testData("TestParseInvalid", Pass, "0.02s"),
		testData("TestParseGoogleTest", Fail, "3.00s"),
		testData("TestFail", Fail, "0.00s"),
		testData("TestSkip", Skip, "0.00s"),
	})
}
