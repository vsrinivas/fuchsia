// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package color

import (
	"fmt"
	"testing"
)

func TestColors(t *testing.T) {
	c := NewColor(ColorAlways)
	colorFns := []Colorfn{c.Black, c.Red, c.Green, c.Yellow, c.Magenta, c.Cyan, c.White, c.DefaultColor}
	colorCodes := []ColorCode{BlackFg, RedFg, GreenFg, YellowFg, MagentaFg, CyanFg, WhiteFg, DefaultFg}

	for i, colorCode := range colorCodes {
		fn := colorFns[i]
		str := fmt.Sprintf("test string: %d", i)
		coloredStr := fn("test string: %d", i)
		withColorStr := c.WithColor(colorCode, "test string: %d", i)
		expectedStr := fmt.Sprintf("%v%vm%v%v", escape, colorCode, str, clear)
		if colorCode == DefaultFg {
			expectedStr = str
		}
		if coloredStr != expectedStr {
			t.Fatalf("Expected string:%v\n, got: %v", expectedStr, coloredStr)
		}
		if withColorStr != expectedStr {
			t.Fatalf("Expected string:%v\n, got: %v", expectedStr, withColorStr)
		}
	}
}

func TestColorsDisabled(t *testing.T) {
	c := NewColor(ColorNever)
	colorFns := []Colorfn{c.Black, c.Red, c.Green, c.Yellow, c.Magenta, c.Cyan, c.White, c.DefaultColor}
	colorCodes := []ColorCode{BlackFg, RedFg, GreenFg, YellowFg, MagentaFg, CyanFg, WhiteFg, DefaultFg}

	for i, colorCode := range colorCodes {
		fn := colorFns[i]
		str := fmt.Sprintf("test string: %d", i)
		coloredStr := fn("test string: %d", i)
		withColorStr := c.WithColor(colorCode, "test string: %d", i)
		if coloredStr != str {
			t.Fatalf("Expected string:%v\n, got: %v", str, coloredStr)
		}
		if withColorStr != str {
			t.Fatalf("Expected string:%v\n, got: %v", str, withColorStr)
		}
	}
}
