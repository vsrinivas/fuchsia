// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"sync"
)

// SourceSet is a convenience class that allows threadsafe ways to interact with
// a set of sources.
type SourceSet struct {
	mu   sync.Mutex
	srcs []Source
}

// AddSource adds a source to our list of sources.
func (s *SourceSet) AddSource(src Source) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.srcs = append(s.srcs, src)
}

// RemoveSource removes a source from our list of sources.
func (s *SourceSet) RemoveSource(src Source) {
	s.mu.Lock()
	defer s.mu.Unlock()

	for i := range s.srcs {
		if s.srcs[i].Equals(src) {
			s.srcs = append(s.srcs[:i], s.srcs[i+1:]...)
			break
		}
	}
}

// Sources returns copy of the list of Sources
func (s *SourceSet) Sources() []Source {
	// lock to prevent getting a list reference whose list is in the middle
	// of updating
	s.mu.Lock()
	c := make([]Source, len(s.srcs))
	copy(c, s.srcs)
	s.mu.Unlock()
	return c
}
