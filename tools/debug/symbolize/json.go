// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"encoding/json"
)

type jsonVisitor struct {
	stack []json.RawMessage
}

func GetLineJson(line []Node) ([]byte, error) {
	var j jsonVisitor
	for _, token := range line {
		token.Accept(&j)
	}
	return j.getJson()
}

func (j *jsonVisitor) getJson() ([]byte, error) {
	return json.MarshalIndent(j.stack, "", "\t")
}

func (j *jsonVisitor) VisitBt(elem *BacktraceElement) {
	type loc struct {
		File     OptStr `json:"file"`
		Line     int    `json:"line"`
		Function OptStr `json:"function"`
	}
	var locs []loc
	for _, srcloc := range elem.info.locs {
		locs = append(locs, loc{
			File:     srcloc.file,
			Line:     srcloc.line,
			Function: srcloc.function,
		})
	}
	msg, _ := json.Marshal(struct {
		Type  string `json:"type"`
		Vaddr uint64 `json:"vaddr"`
		Num   uint64 `json:"num"`
		Locs  []loc  `json:"locs"`
	}{
		Type:  "bt",
		Vaddr: elem.vaddr,
		Num:   elem.num,
		Locs:  locs,
	})
	j.stack = append(j.stack, msg)
}

func (j *jsonVisitor) VisitPc(elem *PCElement) {
	loc := elem.info.locs[0]
	msg, _ := json.Marshal(struct {
		Type     string `json:"type"`
		Vaddr    uint64 `json:"vaddr"`
		File     OptStr `json:"file"`
		Line     int    `json:"line"`
		Function OptStr `json:"function"`
	}{
		Type:     "pc",
		Vaddr:    elem.vaddr,
		File:     loc.file,
		Line:     loc.line,
		Function: loc.function,
	})
	j.stack = append(j.stack, msg)
}

func (j *jsonVisitor) VisitColor(elem *ColorCode) {
	out := j.stack
	msg, _ := json.Marshal(struct {
		Type  string `json:"type"`
		Color uint64 `json:"color"`
	}{
		Type:  "color",
		Color: elem.color,
	})
	j.stack = append(out, msg)
}

func (j *jsonVisitor) VisitText(elem *Text) {
	msg, _ := json.Marshal(struct {
		Type string `json:"type"`
		Text string `json:"text"`
	}{
		Type: "text",
		Text: elem.text,
	})
	j.stack = append(j.stack, msg)
}

func (j *jsonVisitor) VisitDump(elem *DumpfileElement) {
	msg, _ := json.Marshal(struct {
		Type     string `json:"type"`
		SinkType string `json:"sinkType"`
		DumpName string `json:"name"`
	}{
		Type:     "dumpfile",
		SinkType: elem.sinkType,
		DumpName: elem.name,
	})
	j.stack = append(j.stack, msg)
}

// TODO: update this for generalized modules
func (j *jsonVisitor) VisitModule(elem *ModuleElement) {
	msg, _ := json.Marshal(struct {
		Type  string `json:"type"`
		Name  string `json:"name"`
		Build string `json:"build"`
		Id    uint64 `json:"id"`
	}{
		Type:  "module",
		Name:  elem.mod.Name,
		Build: elem.mod.Build,
		Id:    elem.mod.Id,
	})
	j.stack = append(j.stack, msg)
}

func (j *jsonVisitor) VisitReset(elem *ResetElement) {
	msg, _ := json.Marshal(map[string]string{
		"type": "reset",
	})
	j.stack = append(j.stack, msg)
}

// TODO: update this for generalized loads
func (j *jsonVisitor) VisitMapping(elem *MappingElement) {
	msg, _ := json.Marshal(struct {
		Type       string `json:"type"`
		Mod        uint64 `json:"mod"`
		Size       uint64 `json:"size"`
		Vaddr      uint64 `json:"vaddr"`
		Flags      string `json:"flags"`
		ModRelAddr uint64 `json:"modRelAddr"`
	}{
		Type:       "mmap",
		Mod:        elem.seg.Mod,
		Size:       elem.seg.Size,
		Vaddr:      elem.seg.Vaddr,
		Flags:      elem.seg.Flags,
		ModRelAddr: elem.seg.ModRelAddr,
	})
	j.stack = append(j.stack, msg)
}
