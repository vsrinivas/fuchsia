// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package trace

import (
	"bytes"
	"fmt"
	"path/filepath"
	"reflect"
	"runtime"
	"strings"
)

const debugTrace = false
const debugDrop = false
const backtraceDepthMax = 10

var debugFunctions = []uintptr{}

func DebugTraceDeep(depth uint, format string, args ...interface{}) {
	if !debugTrace {
		return
	}
	fmt.Printf("[trace] %s :: at %s\n", fmt.Sprintf(format, args...), backTraceStr(depth))
}

func DebugTrace(format string, args ...interface{}) {
	DebugTraceDeep(1, format, args...)
}

func DebugDropDeep(depth uint, format string, args ...interface{}) {
	if !debugDrop {
		return
	}
	fmt.Printf("[packet dropped] %s :: at %s\n", fmt.Sprintf(format, args...), backTraceStr(depth))
}

func DebugDrop(format string, args ...interface{}) {
	DebugDropDeep(1, format, args...)
}

func backTraceStr(depth uint) (calltrace string) {
	if depth > backtraceDepthMax {
		depth = backtraceDepthMax
	}

	const skipDepth = 2      // Skip Callers(), backTraceStr()
	const skipDebugFuncs = 4 // Skip DebugTrace*(), DebugDrop*()

	pc := make([]uintptr, depth+skipDepth+skipDebugFuncs)

	entry_cnt := runtime.Callers(skipDepth, pc) // Skip minimum necessary
	frames := runtime.CallersFrames(pc[:entry_cnt])

	var buffer bytes.Buffer
	for level := 0; level < entry_cnt; level++ {
		frame, hasNext := frames.Next()
		if isTraceFunc(frame.Entry) {
			continue
		}

		filename := filepath.Base(frame.File)
		fields := strings.Split(frame.Function, ".")
		funcname := fields[len(fields)-1]

		call_str := fmt.Sprintf("%s:%d:%s()", filename, frame.Line, funcname)

		if buffer.Len() > 0 {
			buffer.WriteString(" <- ")
		}
		buffer.WriteString(call_str)

		if !hasNext {
			break
		}
	}

	return buffer.String()
}

func isTraceFunc(f uintptr) bool {
	// Init if not.
	if len(debugFunctions) == 0 {
		// TODO(porce): Reflect this own package or use funcname string
		debugFunctions = []uintptr{
			reflect.ValueOf(DebugTraceDeep).Pointer(),
			reflect.ValueOf(DebugTrace).Pointer(),
			reflect.ValueOf(DebugDropDeep).Pointer(),
			reflect.ValueOf(DebugDrop).Pointer(),
		}
	}

	// Test whether given function is a debug one.
	for _, debugFunc := range debugFunctions {
		if f == debugFunc {
			return true
		}
	}
	return false
}
