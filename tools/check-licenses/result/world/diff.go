// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import "os"

// DiffInfo combines some header information along with the content of
// the NOTICE file that we are diffing against.
//
// This just makes it easier for us to access the information from a template.
type DiffInfo struct {
	Header  []string
	Content []byte
}

func (w *World) SetDiffInfo() error {
	path := Config.DiffNotice
	diffHeader := []string{"Diffing local workspace against " + path}
	diffTarget, err := os.ReadFile(path)
	if err != nil {
		return err
	}

	w.Diff = &DiffInfo{
		Content: diffTarget,
		Header:  diffHeader,
	}

	return nil
}
