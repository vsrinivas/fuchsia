// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build deadlock_detection
// +build deadlock_detection

package sync

import deadlock "github.com/sasha-s/go-deadlock"

type Mutex = deadlock.Mutex
type RWMutex = deadlock.RWMutex
