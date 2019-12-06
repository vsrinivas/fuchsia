// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"encoding/json"
	"io"
)

type DumpEntry struct {
	Modules  []Module  `json:"modules"`
	Segments []Segment `json:"segments"`
	Type     string    `json:"type"`
	Name     string    `json:"name"`
}

type DumpHandler struct {
	dumps []DumpEntry
}

func (d *DumpHandler) HandleDump(dump *DumpfileElement) {
	triggerCtx := dump.Context()
	d.dumps = append(d.dumps, DumpEntry{
		Modules:  triggerCtx.Mods,
		Segments: triggerCtx.Segs,
		Type:     dump.SinkType(),
		Name:     dump.Name(),
	})
}

func (d *DumpHandler) Write(buf io.Writer) error {
	enc := json.NewEncoder(buf)
	enc.SetIndent("", "  ")
	err := enc.Encode(d.dumps)
	if err != nil {
		return err
	}
	return nil
}
