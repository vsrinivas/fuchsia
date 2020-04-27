// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"bytes"
	"encoding/json"
	"testing"
)

func compactJson(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Compact(buffer, jsonBytes)
	return buffer.Bytes()
}

func indentJson(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Indent(buffer, jsonBytes, "", "\t")
	return buffer.Bytes()
}

func testCase(t *testing.T, stdout string, want string) {
	t.Helper()
	actual, _ := json.Marshal(Parse([]byte(stdout)))
	if !bytes.Equal(actual, compactJson([]byte(want))) {
		actualIndented := string(indentJson(actual))
		wantIndented := string(indentJson([]byte(want)))
		t.Errorf("Parse(stdout) = `\n%v\n`; want `\n%v\n``", actualIndented, wantIndented)
	}
}

func TestParseEmpty(t *testing.T) {
	testCase(t, "", "[]")
}

func TestParseInvalid(t *testing.T) {
	stdout := `
Mary had a little lamb
Its fleece was white as snow
And everywhere that Mary went
The lamb was sure to go
`
	testCase(t, stdout, "[]")
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
	want := `
[
	{
        	"name": "SynonymDictTest.IsInitializedEmpty",
        	"status": "Pass",
        	"duration_nanos": 4000000
       	},
       	{
       		"name": "SynonymDictTest.ReadingEmptyFileReturnsFalse",
       		"status": "Pass",
       		"duration_nanos": 3000000
       	},
       	{
       		"name": "SynonymDictTest.ReadingNonexistentFileReturnsFalse",
       		"status": "Pass",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.LoadDictionary",
		"status": "Pass",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.GetSynonymsReturnsListOfWords",
		"status": "Pass",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.GetSynonymsWhenNoSynonymsAreAvailable",
		"status": "Pass",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.AllWordsAreSynonymsOfEachOther",
		"status": "Pass",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.GetSynonymsReturnsListOfWordsWithStubs",
		"status": "Fail",
		"duration_nanos": 4000000
	},
	{
		"name": "SynonymDictTest.CompoundWordBug",
		"status": "Skip",
		"duration_nanos": 4000000
	}
]
`
	testCase(t, stdout, want)
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
	want := `
[
	{
		"name": "TestParseEmpty",
		"status": "Pass",
		"duration_nanos": 10000000
	},
	{
		"name": "TestParseInvalid",
		"status": "Pass",
		"duration_nanos": 20000000
	},
	{
		"name": "TestParseGoogleTest",
		"status": "Fail",
		"duration_nanos": 3000000000
	},
	{
		"name": "TestFail",
		"status": "Fail",
		"duration_nanos": 0
	},
	{
		"name": "TestSkip",
		"status": "Skip",
		"duration_nanos": 0
	}
]
`
	testCase(t, stdout, want)
}
