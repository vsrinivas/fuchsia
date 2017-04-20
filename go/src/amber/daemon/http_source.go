// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"math/rand"
	"os"
	"strings"
	"time"
)

var letters = []rune("1234567890abcdef")

func randSeq(n int) string {
	rand.Seed(time.Now().UnixNano())
	runeLen := len(letters)
	b := make([]rune, n)
	for i := range b {
		b[i] = letters[rand.Intn(runeLen)]
	}
	return string(b)
}

// HTTPSource is an implementation of the PkgFetcher interface that gets
// packages from an Http source. This is currently a dummy placeholder.
type HTTPSource struct {
	addr     string
	interval time.Duration
}

// NewHTTPSource creates an HTTPSource that will pull packages from the HTTP
// location represented in the supplied Source. This retuns an error if the
// Source is not actually an http or https address.
func NewHTTPSource(s string, minInterval time.Duration) (*HTTPSource, error) {
	if strings.Index(s, "http://") != 0 && strings.Index(s, "https://") != 0 {
		return nil, fmt.Errorf("Address does not start with http")
	}

	return &HTTPSource{addr: s, interval: minInterval}, nil
}

func (s *HTTPSource) CheckInterval() time.Duration {
	return s.interval
}

// FetchUpdate NOT currently implemented
func (s *HTTPSource) FetchUpdate(pkg *Package) (*Package, error) {
	// TODO(jmatt) implement
	return &Package{Name: pkg.Name, Version: randSeq(6)}, nil
}

// FetchPkg NOT currently implemented
func (s *HTTPSource) FetchPkg(pkg *Package) (*os.File, error) {
	// TODO(jmatt) implement
	return nil, ErrNoUpdateContent
}

func (s *HTTPSource) Equals(o Source) bool {
	switch t := o.(type) {
	case *HTTPSource:
		return s == t
	default:
		return false
	}
}
