// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"regexp"
	"sync"
	"testing"
)

func TestLicenseAppend(t *testing.T) {
	license := License{
		pattern:      regexp.MustCompile("abcdefghijklmnopqrs\ntuvwxyz"),
		Category:     "alphabet-test",
		matches:      make(map[string]*Match),
		matchChannel: make(chan *Match, 10),
	}
	want := 0
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		license.MatchChannelWorker()
		wg.Done()
	}()
	license.appendFile("test_path_0")
	license.AddMatch(nil)
	wg.Wait()

	want = 1
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}

	if len(license.matches[""].files) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches[""].files), want)
	}
}

func TestParseAuthor(t *testing.T) {
	data := []string{
		"Copyright (C) 2020 Foo All rights reserved",
		"Copyright © 2020 Foo All rights reserved",
		"Copyright © 2020 Foo",
	}
	for _, in := range data {
		if m := parseAuthor(in); m != "Foo" {
			t.Errorf("%q failed, got %q", in, m)
		}
	}
}

func TestGetAuthorMatches(t *testing.T) {
	data := []string{
		"Copyright (C) 2020 Foo All rights reserved",
		"Copyright © 2020 Foo All rights reserved",
		"Copyright © 2020 Foo",
	}
	for _, in := range data {
		if m := getAuthorMatches([]byte(in)); len(m) == 0 {
			t.Errorf("%q failed, got %q", in, m)
		}
	}
}
