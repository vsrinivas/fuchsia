// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"time"
)

// NewUpdateRequest creates a new UpdateRequest object
func NewUpdateRequest(targets []Package, inter time.Duration) *UpdateRequest {
	return &UpdateRequest{
		Targets:        &targets,
		UpdateInterval: inter,
	}
}

// A UpdateRequest is used to ask the Dispatcher to monitor a package.
type UpdateRequest struct {
	Targets *[]Package
	// UpdateInterval is the desired period of time between checks. If an
	// update source doesn't allow polling this frequently, checks will
	// occur less often. Note that intervals less than a second are not
	// well-supported owing to accuracy of timers
	UpdateInterval time.Duration
}

func (req *UpdateRequest) GetTargets() []Package {
	return *req.Targets
}
