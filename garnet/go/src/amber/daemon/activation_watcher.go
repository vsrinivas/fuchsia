// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"sync"
)

type activationWatcher struct {
	mu        sync.Mutex
	observers map[string][]func(string, error)
}

func (a *activationWatcher) addWatch(root string, f func(string, error)) {
	a.mu.Lock()
	defer a.mu.Unlock()

	if a.observers == nil {
		a.observers = make(map[string][]func(string, error))
	}

	a.observers[root] = append(a.observers[root], f)
}

func (a *activationWatcher) update(root string, err error) {
	a.mu.Lock()
	fs := a.observers[root]
	delete(a.observers, root)
	a.mu.Unlock()

	for _, f := range fs {
		f(root, err)
	}
}
