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
		category:     "alphabet-test",
		matches:      make(map[string]*Match),
		matchChannel: make(chan *Match, 10),
	}
	want := 0
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}

	wg := setupLicenseWorker(&license)
	license.append("test_path_0")
	closeLicenseWorker(&license, wg)

	want = 1
	if len(license.matches) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches), want)
	}

	if len(license.matches[""].files) != want {
		t.Errorf("%v(): got %v, want %v", t.Name(), len(license.matches[""].files), want)
	}
}

func setupLicenseWorker(l *License) *sync.WaitGroup {
	// Start worker channel.
	var wg sync.WaitGroup
	wg.Add(1)
	go l.MatchChannelWorker(&wg)
	return &wg
}

func closeLicenseWorker(l *License, wg *sync.WaitGroup) {
	// Close worker channel.
	l.AddMatch(nil)
	wg.Wait()
}
