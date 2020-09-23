// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"
	"log"
)

// CodeGenerator represents a code generator which takes a graph of
// `MeasureTape` and creates all the needed methods to measure their
// size.
type CodeGenerator struct {
	toProcess []*MeasuringTape
	done      map[*MeasuringTape]struct{}
}

// NewCodeGenerator creates a new code generator.
func NewCodeGenerator(mt *MeasuringTape) *CodeGenerator {
	return &CodeGenerator{
		toProcess: []*MeasuringTape{mt},
		done:      make(map[*MeasuringTape]struct{}),
	}
}

// Generate generates and optimizes the code.
//
// This should be called once, and the result saved for further processing or
// printing.
func (cg *CodeGenerator) Generate() map[MethodID]*Method {
	allMethods := cg.genAllMethods()
	pruneEmptyMethods(allMethods)
	return allMethods
}

func (cg *CodeGenerator) genAllMethods() map[MethodID]*Method {
	allMethods := make(map[MethodID]*Method)
	for {
		mt := cg.nextMt()
		if mt == nil {
			return allMethods
		}
		// TODO(fxbug.dev/51368): Variable naming should be defered to printing.
		local := exprLocal("value", mt.kind, false)
		for _, m := range []*Method{
			cg.newMeasureMethod(mt, local),
			cg.newMeasureOutOfLineMethod(mt, local),
			cg.newMeasureHandlesMethod(mt, local),
		} {
			allMethods[m.ID] = m
		}
	}
}

func (cg *CodeGenerator) add(mt *MeasuringTape) {
	if _, ok := cg.done[mt]; !ok {
		cg.toProcess = append(cg.toProcess, mt)
	}
}

func (cg *CodeGenerator) nextMt() *MeasuringTape {
	for {
		if len(cg.toProcess) == 0 {
			return nil
		}

		var mt *MeasuringTape
		mt, cg.toProcess = cg.toProcess[0], cg.toProcess[1:]

		if _, ok := cg.done[mt]; !ok {
			return mt
		}
	}
}

func (mt *MeasuringTape) assertOnlyStructUnionTable() {
	switch mt.kind {
	case Union:
		return
	case Struct:
		return
	case Table:
		return
	default:
		log.Panicf("should not be reachable for kind %v", mt.kind)
	}
}

func (cg *CodeGenerator) newMeasureMethod(mt *MeasuringTape, expr Expression) *Method {
	mt.assertOnlyStructUnionTable()

	var body Block
	switch mt.kind {
	case Union:
		body.emitAddNumBytes(exprNum(mt.inlineNumBytes))
	case Struct:
		body.emitAddNumBytes(exprFidlAlign(exprNum(mt.inlineNumBytes)))
		if mt.hasHandles {
			body.emitInvoke(mt.methodIDOf(MeasureHandles), expr)
		}
	case Table:
		body.emitAddNumBytes(exprNum(mt.inlineNumBytes))
	}
	body.emitInvoke(mt.methodIDOf(MeasureOutOfLine), expr)
	return newMethod(mt.methodIDOf(Measure), expr, &body)
}

func (cg *CodeGenerator) newMeasureOutOfLineMethod(mt *MeasuringTape, expr Expression) *Method {
	mt.assertOnlyStructUnionTable()

	var body Block
	switch mt.kind {
	case Union:
		cg.writeUnionOutOfLine(mt, expr, &body)
	case Struct:
		cg.writeStructOutOfLine(mt, expr, &body)
	case Table:
		cg.writeTableOutOfLine(mt, expr, &body)
	}
	return newMethod(mt.methodIDOf(MeasureOutOfLine), expr, &body)
}

func (cg *CodeGenerator) newMeasureHandlesMethod(mt *MeasuringTape, expr Expression) *Method {
	mt.assertOnlyStructUnionTable()

	var body Block
	if mt.hasHandles {
		switch mt.kind {
		case Struct:
			for _, member := range mt.members {
				if member.mt.kind == Handle {
					// TODO(fxbug.dev/49488): Conditionally increase for nullable handles.
					body.emitAddNumHandles(exprNum(1))
				} else if member.mt.hasHandles {
					body.emitInvoke(
						member.mt.methodIDOf(MeasureHandles),
						exprMemberOf(expr, member.name, member.mt.kind, member.mt.nullable))
				}
			}
		}
	}
	return newMethod(mt.methodIDOf(MeasureHandles), expr, &body)
}

type invokeKind int

const (
	_ invokeKind = iota
	inlineAndOutOfLine
	outOfLineOnly
)

func guardNullableAccess(member measuringTapeMember, expr Expression, body *Block, fn func(*Block)) {
	if member.mt.nullable {
		var guardBody Block
		body.emitGuard(expr, &guardBody)
		fn(&guardBody)
	} else {
		fn(body)
	}
}

func (cg *CodeGenerator) writeInvoke(member measuringTapeMember, expr Expression, body *Block, mode invokeKind) {
	switch member.mt.kind {
	case String:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(16))
		}
		guardNullableAccess(member, expr, body, func(guardBody *Block) {
			guardBody.emitAddNumBytes(exprFidlAlign(exprLength(expr)))
		})
	case Vector:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(16))
		}
		guardNullableAccess(member, expr, body, func(guardBody *Block) {
			if mode == inlineAndOutOfLine {
				if member.mt.elementMt.kind == Handle ||
					(!member.mt.elementMt.hasOutOfLine && member.mt.elementMt.hasHandles) {
					// TODO(fxbug.dev/49488): Conditionally increase for nullable handles.
					guardBody.emitAddNumHandles(
						exprMult(
							exprLength(expr),
							exprNum(member.mt.elementMt.inlineNumHandles)))
				}
			}
			if member.mt.elementMt.hasOutOfLine {
				memberMt := measuringTapeMember{
					name: fmt.Sprintf("%s_elem", member.name),
					mt:   member.mt.elementMt,
				}
				var iterateBody Block
				local := exprLocal(memberMt.name, memberMt.mt.kind, memberMt.mt.nullable)
				guardBody.emitIterate(
					local, expr,
					&iterateBody)
				cg.writeInvoke(
					memberMt, local, &iterateBody, inlineAndOutOfLine)
			} else {
				guardBody.emitAddNumBytes(
					exprFidlAlign(
						exprMult(
							exprLength(expr),
							exprNum(member.mt.elementMt.inlineNumBytes))))
			}
		})
	case Array:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprFidlAlign(exprNum(member.mt.inlineNumBytes)))
			if member.mt.elementMt.kind == Handle || (!member.mt.hasOutOfLine && member.mt.hasHandles) {
				// TODO(fxbug.dev/49488): Conditionally increase for nullable handles.
				body.emitAddNumHandles(exprNum(member.mt.inlineNumHandles))
			}
		}
		if member.mt.hasOutOfLine {
			memberMt := measuringTapeMember{
				name: fmt.Sprintf("%s_elem", member.name),
				mt:   member.mt.elementMt,
			}
			var iterateBody Block
			local := exprLocal(memberMt.name, memberMt.mt.kind, memberMt.mt.nullable)
			body.emitIterate(
				local, expr,
				&iterateBody)
			cg.writeInvoke(
				memberMt, local, &iterateBody, outOfLineOnly)
		}
	case Primitive:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(8))
		}
	case Handle:
		if mode == inlineAndOutOfLine {
			body.emitAddNumBytes(exprNum(8))
			// TODO(fxbug.dev/49488): Conditionally increase for nullable handles.
			body.emitAddNumHandles(exprNum(1))
		}
	default:
		cg.add(member.mt)
		guardNullableAccess(member, expr, body, func(guardBody *Block) {
			switch mode {
			case inlineAndOutOfLine:
				guardBody.emitInvoke(member.mt.methodIDOf(Measure), expr)
			case outOfLineOnly:
				guardBody.emitInvoke(member.mt.methodIDOf(MeasureOutOfLine), expr)
			}
		})
	}
}

func (cg *CodeGenerator) writeStructOutOfLine(mt *MeasuringTape, expr Expression, body *Block) {
	for _, member := range mt.members {
		cg.writeInvoke(
			member,
			exprMemberOf(expr, member.name, member.mt.kind, member.mt.nullable),
			body, outOfLineOnly)
	}
}

func (cg *CodeGenerator) writeUnionOutOfLine(mt *MeasuringTape, expr Expression, body *Block) {
	variants := make(map[string]LocalWithBlock)

	// known
	for _, member := range mt.members {
		var variantBody Block
		local := exprLocal("_"+member.name, member.mt.kind, member.mt.nullable)
		variants[member.name] = LocalWithBlock{
			Local: local,
			Body:  &variantBody,
		}
		cg.writeInvoke(member, local, &variantBody, inlineAndOutOfLine)
	}

	// unknown
	if mt.isFlexible {
		var variantBody Block
		variantBody.emitMaxOut()
		variants[UnknownVariant] = LocalWithBlock{
			Body: &variantBody,
		}
	}

	body.emitSelectVariant(expr, mt.name, variants)
}

func (cg *CodeGenerator) writeTableOutOfLine(mt *MeasuringTape, expr Expression, body *Block) {
	maxOrdinalLocal := body.emitDeclareMaxOrdinal()
	for _, member := range mt.members {
		var guardBody Block
		body.emitGuard(
			exprHasMember(expr, member.name),
			&guardBody)
		cg.writeInvoke(
			member,
			exprMemberOf(expr, member.name, member.mt.kind, member.mt.nullable),
			&guardBody, inlineAndOutOfLine)
		guardBody.emitSetMaxOrdinal(maxOrdinalLocal, member.ordinal)
	}
	body.emitAddNumBytes(exprMult(exprNum(16), maxOrdinalLocal))
}
