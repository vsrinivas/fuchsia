// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"sort"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
)

func TestValidateGood(t *testing.T) {
	testCases := []string{
		`{
			"test_goldens_dir": "a",
			"regen_goldens_dir": "b"
		}`,
		`{
			"test_goldens_dir": "a",
			"regen_goldens_dir": "b",
			"entries": []
		}`,
		`{
			"test_goldens_dir": "a",
			"regen_goldens_dir": "b",
			"entries": [
				{"golden": "c.golden", "generated": "d"},
				{"golden": "e.golden", "generated": "g"}
			]
		}`,
	}
	for _, tc := range testCases {
		var m manifest
		if err := json.Unmarshal([]byte(tc), &m); err != nil {
			t.Errorf("unmarshaling failed: %s. manifest:\n%s", err, tc)
		} else if err := m.validate(); err != nil {
			t.Errorf("want nil, got: %s. manifest:\n%s", err, tc)
		}
	}
}

func TestValidateBad(t *testing.T) {
	testCases := []struct {
		manifestJSON string
		errSubstring string
	}{
		{
			`{"test_goldens_dir": "a"}`,
			"missing regen dir",
		},
		{
			`{"regen_goldens_dir": "a"}`,
			"missing test dir",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [{"golden": "c"}]
			}`,
			"missing generated path",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [{"generated": "c"}]
			}`,
			"missing golden path",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [{"golden": "c/d.golden", "generated": "e"}]
			}`,
			"c/d.golden: subdirectories not allowed",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [{"golden": "c", "generated": "d"}]
			}`,
			"c: expected .golden",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [{"golden": "c.golden", "generated": "d.golden"}]
			}`,
			"d.golden: unexpected .golden",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [
					{"golden": "c.golden", "generated": "d"},
					{"golden": "c.golden", "generated": "e"}
				]
			}`,
			"c.golden: duplicate golden",
		},
		{
			`{
				"test_goldens_dir": "a",
				"regen_goldens_dir": "b",
				"entries": [
					{"golden": "c.golden", "generated": "d"},
					{"golden": "e.golden", "generated": "d"}
				]
			}`,
			"d: duplicate generated",
		},
	}
	for _, tc := range testCases {
		var m manifest
		if err := json.Unmarshal([]byte(tc.manifestJSON), &m); err != nil {
			t.Errorf("unmarshaling failed: %s. manifest:\n%s", err, tc.manifestJSON)
		} else if err := m.validate(); err == nil || !strings.Contains(err.Error(), tc.errSubstring) {
			t.Errorf("want err containing %q, got: %v. manifest:\n%s", tc.errSubstring, err, tc.manifestJSON)
		}
	}
}

type testFixture struct {
	*testing.T
	tempDirs map[string]string
}

func newTestFixture(t *testing.T) testFixture {
	return testFixture{
		T:        t,
		tempDirs: make(map[string]string),
	}
}

func (t testFixture) createTempDirs(names ...string) {
	t.Helper()
	for _, name := range names {
		if name[0] != '$' {
			panic("names must start with $")
		}
		t.tempDirs[name] = t.TempDir()
	}
}

// fmt replaces temporary directory names in expr with their paths. For example,
// if you call t.createTempDirs("$FOO") beforehand, fmt will replace all
// occurrences of "$FOO" with the path to that temporary directory.
func (t testFixture) fmt(expr string) string {
	res := expr
	for name, dir := range t.tempDirs {
		res = strings.ReplaceAll(res, name, dir)
	}
	if strings.Contains(res, "$") {
		panic(fmt.Sprintf("undefined variable in %q", expr))
	}
	return res
}

func (t testFixture) writeFile(filenameExpr, contentExpr string) {
	t.Helper()
	filename := t.fmt(filenameExpr)
	content := t.fmt(contentExpr)
	if err := os.WriteFile(filename, []byte(content), 0o666); err != nil {
		t.Fatal(err)
	}
}

func (t testFixture) parseManifest(jsonExpr string) manifest {
	t.Helper()
	jsonStr := t.fmt(jsonExpr)
	var m manifest
	if err := json.Unmarshal([]byte(jsonStr), &m); err != nil {
		t.Fatalf("unmarshaling failed: %s. manifest:\n%s", err, jsonStr)
	} else if err := m.validate(); err != nil {
		t.Fatalf("invalid manifest: %s. manifest:\n%s", err, jsonStr)
	}
	return m
}

func (t testFixture) assertDir(pathExpr string, filenames []string) {
	t.Helper()
	sort.Strings(filenames)
	entries, err := os.ReadDir(t.fmt(pathExpr))
	if err != nil {
		t.Fatal(err)
	}
	var got []string
	for _, e := range entries {
		got = append(got, e.Name())
	}
	if diff := cmp.Diff(filenames, got); diff != "" {
		t.Fatalf("unexpected files (-want +got):\n%s", diff)
	}
}

func (t testFixture) assertFile(pathExpr, content string) {
	t.Helper()
	got, err := os.ReadFile(t.fmt(pathExpr))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(content, string(got)); diff != "" {
		t.Fatalf("unexpected content (-want +got):\n%s", diff)
	}
}

func (t testFixture) assertNotModified(pathExpr string, during func()) {
	t.Helper()
	path := t.fmt(pathExpr)
	// First, reset mtime to the epoch. This avoids the false positive where the
	// test executes so fast that a modification does not change mtime.
	longAgo := time.Unix(0, 0)
	if err := os.Chtimes(path, longAgo, longAgo); err != nil {
		t.Fatal(err)
	}
	during()
	if info, err := os.Stat(path); err != nil {
		t.Fatal(err)
	} else if !info.ModTime().Equal(longAgo) {
		t.Fatalf("%s was modified at %s", pathExpr, info.ModTime())
	}
}

// printIndented prints s to standard output with every line indented by a tab.
// Without indentation, main_test.go output is extremely confusing since
// golden-util mimics go test with "--- PASS", "--- FAIL", etc.
func printIndented(s string) {
	if s == "" {
		return
	}
	s, last := s[:len(s)-1], s[len(s)-1]
	fmt.Print("\t")
	fmt.Print(strings.ReplaceAll(s, "\n", "\n\t"))
	fmt.Print(string(last))
}

func (t testFixture) assertRegen(m manifest) {
	t.Helper()
	var buf bytes.Buffer
	if err := m.regen(&buf); err != nil {
		printIndented(buf.String())
		t.Fatalf("want nil, got: %s", err)
	}
}

func (t testFixture) assertRegenErr(m manifest, errSubstring string) {
	t.Helper()
	var buf bytes.Buffer
	if err := m.regen(&buf); err == nil || !strings.Contains(err.Error(), errSubstring) {
		printIndented(buf.String())
		t.Fatalf("want err substring %q, got: %v", errSubstring, err)
	}
}

func (t testFixture) assertTest(m manifest) {
	t.Helper()
	var buf bytes.Buffer
	if ok, err := m.test(&buf); err != nil {
		printIndented(buf.String())
		t.Fatalf("want nil, got: %s", err)
	} else if !ok {
		printIndented(buf.String())
		t.Fatal("test failed")
	}
}

func (t testFixture) assertTestErr(m manifest, errSubstring string) {
	t.Helper()
	var buf bytes.Buffer
	if _, err := m.test(&buf); err == nil || !strings.Contains(err.Error(), errSubstring) {
		printIndented(buf.String())
		t.Fatalf("want err substring %q, got: %v", errSubstring, err)
	}
}

func (t testFixture) assertTestFails(m manifest) {
	t.Helper()
	var buf bytes.Buffer
	if ok, err := m.test(&buf); err != nil {
		printIndented(buf.String())
		t.Fatalf("want nil, got: %s", err)
	} else if ok {
		printIndented(buf.String())
		t.Fatal("test unexpectedly passed")
	}
}

func TestNoGoldens(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A")
	t.writeFile("$A/goldens.txt", "")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": []
	}`)

	// Regen does nothing.
	t.assertRegen(m)
	t.assertDir("$A", []string{"goldens.txt"})
	t.assertFile("$A/goldens.txt", "")
	// Test passes since there is nothing to compare.
	t.assertTest(m)
}

func TestSetUpGoldens(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "foo contents")
	t.writeFile("$B/bar", "bar contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)

	// Test fails at first: all goldens are missing.
	t.assertTestFails(m)
	// Regen writes all the golden files.
	t.assertRegen(m)
	t.assertDir("$A", []string{"goldens.txt", "foo.golden", "bar.golden"})
	t.assertFile("$A/goldens.txt", "bar.golden\nfoo.golden\n")
	t.assertFile("$A/foo.golden", "foo contents")
	t.assertFile("$A/bar.golden", "bar contents")
	// Now the test passes.
	t.assertTest(m)
}

func TestUpdateGolden(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "won't change")
	t.writeFile("$B/bar", "will change")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)
	t.assertRegen(m)

	// Change bar. The test fails because bar.golden doesn't match.
	t.writeFile("$B/bar", "will change -- NOW!")
	t.assertTestFails(m)
	// Regen updates bar.golden.
	t.assertRegen(m)
	t.assertFile("$A/foo.golden", "won't change")
	t.assertFile("$A/bar.golden", "will change -- NOW!")
	// Now the test passes.
	t.assertTest(m)
}

func TestAddGolden(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "foo contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [{"golden": "foo.golden", "generated": "$B/foo"}]
	}`)
	t.assertRegen(m)

	// Create bar and add bar.golden to the manifest.
	t.writeFile("$B/bar", "bar contents")
	m = t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)
	// Test fails because bar.golden is missing.
	t.assertTestFails(m)
	// Regen writes bar.golden.
	t.assertRegen(m)
	t.assertDir("$A", []string{"goldens.txt", "foo.golden", "bar.golden"})
	t.assertFile("$A/goldens.txt", "bar.golden\nfoo.golden\n")
	// Now the test passes.
	t.assertTest(m)
}

func TestRemoveGolden(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "foo contents")
	t.writeFile("$B/bar", "bar contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)
	t.assertRegen(m)

	// Remove bar.golden from the manifest.
	m = t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [{"golden": "foo.golden", "generated": "$B/foo"}]
	}`)
	// Test fails because goldens.txt has an extra file, bar.golden.
	t.assertTestFails(m)
	// Regen removes bar.golden.
	t.assertRegen(m)
	t.assertDir("$A", []string{"goldens.txt", "foo.golden"})
	t.assertFile("$A/goldens.txt", "foo.golden\n")
	// Now the test passes.
	t.assertTest(m)
}

func TestIdempotence(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "foo contents")
	t.writeFile("$B/bar", "bar contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)

	// Additional regens have no effect.
	t.assertRegen(m)
	t.assertRegen(m)
	// The test doesn't mutate anything.
	t.assertTest(m)
	t.assertTest(m)
}

func TestRegenRemovesUntrackedGoldenFiles(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$A/why_is_this_here.golden", "delete me!")
	t.writeFile("$A/or_this.golden", "delete me too!")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": []
	}`)

	// The test is not affected (it only cares about what's in goldens.txt).
	t.assertTest(m)
	// But regen will remove the untracked files for us.
	t.assertRegen(m)
	t.assertDir("$A", []string{"goldens.txt"})
	t.assertFile("$A/goldens.txt", "")
}

func TestRegenLeavesAccurateGoldensTxtOnError(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "foo contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)

	// Regen fails because $B/bar does not exist.
	t.assertRegenErr(m, "bar")
	// But it should leave goldens.txt consistent with the filesystem.
	t.assertDir("$A", []string{"goldens.txt", "foo.golden"})
	t.assertFile("$A/goldens.txt", "foo.golden\n")
}

func TestRegenDoesNotRewriteUnchangedGoldensTxt(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	// Add extra blank lines and use non-alphabetical order to show that regen
	// uses set equality when determining whether to rewrite goldens.txt.
	goldensTxt := "\n\n\nfoo.golden\n\nbar.golden\n\n\n"
	t.writeFile("$A/goldens.txt", goldensTxt)
	t.writeFile("$A/foo.golden", "old")
	t.writeFile("$A/bar.golden", "old")
	t.writeFile("$B/foo", "new")
	t.writeFile("$B/bar", "new")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)

	// Regen rewrites foo.golden and bar.golden, but not goldens.txt.
	t.assertNotModified("$A/goldens.txt", func() {
		t.assertRegen(m)
	})
	t.assertFile("$A/goldens.txt", goldensTxt)
}

func TestRegenDoesNotRewriteUnchangedGoldenFile(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "foo.golden")
	t.writeFile("$A/foo.golden", "unchanged")
	t.writeFile("$B/foo", "unchanged")
	t.writeFile("$B/bar", "new file")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"}
		]
	}`)

	// Regen rewrites goldens.txt, but not foo.golden.
	t.assertNotModified("$A/foo.golden", func() {
		t.assertRegen(m)
	})
	t.assertFile("$A/foo.golden", "unchanged")
}

func TestChangesWithVariousSizes(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "")
	t.writeFile("$B/foo", "same")
	t.writeFile("$B/bar", "grow")
	t.writeFile("$B/baz", "shrink")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [
			{"golden": "foo.golden", "generated": "$B/foo"},
			{"golden": "bar.golden", "generated": "$B/bar"},
			{"golden": "baz.golden", "generated": "$B/baz"}
		]
	}`)
	t.assertRegen(m)

	// Exercise code paths that check size before comparing content.
	t.writeFile("$B/foo", "SAME")
	t.writeFile("$B/bar", "grow larger")
	t.writeFile("$B/baz", "")
	t.assertRegen(m)
	t.assertFile("$A/foo.golden", "SAME")
	t.assertFile("$A/bar.golden", "grow larger")
	t.assertFile("$A/baz.golden", "")
	t.assertTest(m)
}

func TestSeparateDirsForRegenAndTest(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "foo.golden")
	t.writeFile("$B/foo", "foo contents")
	t.assertRegen(t.parseManifest(`{
		"test_goldens_dir": "(UNUSED FOR REGEN)",
		"regen_goldens_dir": "$A",
		"entries": [{"golden": "foo.golden", "generated": "$B/foo"}]
	}`))
	t.assertTest(t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "(UNUSED FOR TEST)",
		"entries": [{"golden": "foo.golden", "generated": "$B/foo"}]
	}`))
}

func TestFailsWithoutGoldensTxt(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": []
	}`)
	t.assertRegenErr(m, "goldens.txt")
	t.assertTestErr(m, "goldens.txt")
}

func TestFailsWithInvalidGoldensTxt(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": []
	}`)

	t.writeFile("$A/goldens.txt", "subdirs_are/not_allowed.golden")
	t.assertRegenErr(m, "subdirectories not allowed")
	t.assertTestErr(m, "subdirectories not allowed")

	t.writeFile("$A/goldens.txt", "not_golden_extension.c")
	t.assertRegenErr(m, "expected .golden extension")
	t.assertTestErr(m, "expected .golden extension")
}

func TestFailsWithMissingGoldenFile(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "foo.golden")
	t.writeFile("$B/foo", "foo contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [{"golden": "foo.golden", "generated": "$B/foo"}]
	}`)
	t.assertTestFails(m)
}

func TestFailsWithMissingGeneratedFile(pt *testing.T) {
	t := newTestFixture(pt)
	t.createTempDirs("$A", "$B")
	t.writeFile("$A/goldens.txt", "foo.golden")
	t.writeFile("$A/foo.golden", "foo contents")
	m := t.parseManifest(`{
		"test_goldens_dir": "$A",
		"regen_goldens_dir": "$A",
		"entries": [{"golden": "foo.golden", "generated": "$B/does_not_exist"}]
	}`)
	t.assertRegenErr(m, "does_not_exist")
	t.assertTestFails(m)
}
