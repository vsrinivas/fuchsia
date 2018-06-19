// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"encoding/json"
)

type JsonVisitor struct {
	stack []json.RawMessage
}

func GetLineJson(line []Node) ([]byte, error) {
	var j JsonVisitor
	for _, token := range line {
		token.Accept(&j)
	}
	return j.getJson()
}

func (j *JsonVisitor) getJson() ([]byte, error) {
	return json.MarshalIndent(j.stack, "", "\t")
}

func (j *JsonVisitor) VisitBt(elem *BacktraceElement) {
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
		Tipe  string `json:"type"`
		Vaddr uint64 `json:"vaddr"`
		Num   uint64 `json:"num"`
		Locs  []loc  `json:"locs"`
	}{
		Tipe:  "bt",
		Vaddr: elem.vaddr,
		Num:   elem.num,
		Locs:  locs,
	})
	j.stack = append(j.stack, msg)
}

func (j *JsonVisitor) VisitPc(elem *PCElement) {
	loc := elem.info.locs[0]
	msg, _ := json.Marshal(struct {
		Tipe     string `json:"type"`
		Vaddr    uint64 `json:"vaddr"`
		File     OptStr `json:"file"`
		Line     int    `json:"line"`
		Function OptStr `json:"function"`
	}{
		Tipe:     "pc",
		Vaddr:    elem.vaddr,
		File:     loc.file,
		Line:     loc.line,
		Function: loc.function,
	})
	j.stack = append(j.stack, msg)
}

func (j *JsonVisitor) VisitColor(elem *ColorCode) {
	out := j.stack
	msg, _ := json.Marshal(struct {
		Tipe  string `json:"type"`
		Color uint64 `json:"color"`
	}{
		Tipe:  "color",
		Color: elem.color,
	})
	j.stack = append(out, msg)
}

func (j *JsonVisitor) VisitText(elem *Text) {
	msg, _ := json.Marshal(struct {
		Tipe string `json:"type"`
		Text string `json:"text"`
	}{
		Tipe: "text",
		Text: elem.text,
	})
	j.stack = append(j.stack, msg)
}

// TODO: update this for generalized modules
func (j *JsonVisitor) VisitModule(elem *ModuleElement) {
	msg, _ := json.Marshal(struct {
		Tipe  string `json:"type"`
		Name  string `json:"name"`
		Build string `json:"build"`
		Id    uint64 `json:"id"`
	}{
		Tipe:  "module",
		Name:  elem.mod.name,
		Build: elem.mod.build,
		Id:    elem.mod.id,
	})
	j.stack = append(j.stack, msg)
}

func (j *JsonVisitor) VisitReset(elem *ResetElement) {
	msg, _ := json.Marshal(map[string]string{
		"type": "reset",
	})
	j.stack = append(j.stack, msg)
}

// TODO: update this for generalized loads
func (j *JsonVisitor) VisitMapping(elem *MappingElement) {
	msg, _ := json.Marshal(struct {
		Tipe       string `json:"type"`
		Mod        uint64 `json:"mod"`
		Size       uint64 `json:"size"`
		Vaddr      uint64 `json:"vaddr"`
		Flags      string `json:"flags"`
		ModRelAddr uint64 `json:"modRelAddr"`
	}{
		Tipe:       "mmap",
		Mod:        elem.seg.mod,
		Size:       elem.seg.size,
		Vaddr:      elem.seg.vaddr,
		Flags:      elem.seg.flags,
		ModRelAddr: elem.seg.modRelAddr,
	})
	j.stack = append(j.stack, msg)
}
