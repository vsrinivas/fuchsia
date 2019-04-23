// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"encoding/json"
	"os"
	"time"
)

type UpdateHistory struct {
	// A prior UpdateHistory is replaced when SourceVersion or TargetVersion differ
	SourceVersion string    `json:"source"`
	TargetVersion string    `json:"target"`
	StartTime     time.Time `json:"start"`
	Attempts      int       `json:"attempts"`
}

const historyPath = "/data/update_history.json"

// IncrementOrCreateUpdateHistory loads the last update history struct from
// disk, incrementing its attempt counter, or starts over with a fresh history
// struct if the load fails or this update attempt's SourceVersion or
// TargetVersion differ.
func IncrementOrCreateUpdateHistory(sourceVersion string, targetVersion string, startTime time.Time) UpdateHistory {
	firstTry := UpdateHistory{
		SourceVersion: sourceVersion,
		TargetVersion: targetVersion,
		StartTime:     startTime,
		Attempts:      1,
	}

	f, err := os.Open(historyPath)
	if err != nil {
		return firstTry
	}
	defer f.Close()

	var res UpdateHistory
	if err := json.NewDecoder(f).Decode(&res); err != nil {
		return firstTry
	}

	if res.SourceVersion != sourceVersion || res.TargetVersion != targetVersion {
		return firstTry
	}

	res.StartTime = startTime
	res.Attempts++
	return res
}

// Save writes the given update history struct to disk.
func (h *UpdateHistory) Save() error {
	f, err := os.Create(historyPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := json.NewEncoder(f).Encode(h); err != nil {
		return err
	}
	f.Sync()

	return nil
}
