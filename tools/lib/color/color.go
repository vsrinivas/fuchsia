// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package color provides functions for printing in different colors.
package color

import (
	"fmt"
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/isatty"
)

// Colorfn is a function type that takes a formatted string and returns it in a certain color.
type Colorfn func(format string, a ...interface{}) string

const (
	escape = "\033["
	clear  = escape + "0m"
)

// ColorCode represents the int used for coloring formatted strings in a certain color.
type ColorCode int

// Foreground text colors
const (
	BlackFg ColorCode = iota + 30
	RedFg
	GreenFg
	YellowFg
	BlueFg
	MagentaFg
	CyanFg
	WhiteFg
	DefaultFg
)

// Color provides an interface for a color or monochrome type that will be returned by NewColor()
// based on whether coloring is enabled or not.
type Color interface {
	Black(format string, a ...interface{}) string
	Red(format string, a ...interface{}) string
	Green(format string, a ...interface{}) string
	Yellow(format string, a ...interface{}) string
	Blue(format string, a ...interface{}) string
	Magenta(format string, a ...interface{}) string
	Cyan(format string, a ...interface{}) string
	White(format string, a ...interface{}) string
	DefaultColor(format string, a ...interface{}) string
	WithColor(code ColorCode, format string, a ...interface{}) string
	Enabled() bool
}

type color struct{}

func (color) Black(format string, a ...interface{}) string { return colorString(BlackFg, format, a...) }
func (color) Red(format string, a ...interface{}) string   { return colorString(RedFg, format, a...) }
func (color) Green(format string, a ...interface{}) string { return colorString(GreenFg, format, a...) }
func (color) Yellow(format string, a ...interface{}) string {
	return colorString(YellowFg, format, a...)
}
func (color) Blue(format string, a ...interface{}) string { return colorString(BlueFg, format, a...) }
func (color) Magenta(format string, a ...interface{}) string {
	return colorString(MagentaFg, format, a...)
}
func (color) Cyan(format string, a ...interface{}) string  { return colorString(CyanFg, format, a...) }
func (color) White(format string, a ...interface{}) string { return colorString(WhiteFg, format, a...) }
func (color) DefaultColor(format string, a ...interface{}) string {
	return colorString(DefaultFg, format, a...)
}

func (color) WithColor(code ColorCode, format string, a ...interface{}) string {
	return colorString(code, format, a...)
}

func (color) Enabled() bool {
	return true
}

func colorString(c ColorCode, format string, a ...interface{}) string {
	if c == DefaultFg {
		return fmt.Sprintf(format, a...)
	}
	return fmt.Sprintf("%v%vm%v%v", escape, c, fmt.Sprintf(format, a...), clear)
}

type monochrome struct{}

func (monochrome) Black(format string, a ...interface{}) string   { return fmt.Sprintf(format, a...) }
func (monochrome) Red(format string, a ...interface{}) string     { return fmt.Sprintf(format, a...) }
func (monochrome) Green(format string, a ...interface{}) string   { return fmt.Sprintf(format, a...) }
func (monochrome) Yellow(format string, a ...interface{}) string  { return fmt.Sprintf(format, a...) }
func (monochrome) Blue(format string, a ...interface{}) string    { return fmt.Sprintf(format, a...) }
func (monochrome) Magenta(format string, a ...interface{}) string { return fmt.Sprintf(format, a...) }
func (monochrome) Cyan(format string, a ...interface{}) string    { return fmt.Sprintf(format, a...) }
func (monochrome) White(format string, a ...interface{}) string   { return fmt.Sprintf(format, a...) }
func (monochrome) DefaultColor(format string, a ...interface{}) string {
	return fmt.Sprintf(format, a...)
}

func (monochrome) WithColor(_ ColorCode, format string, a ...interface{}) string {
	return fmt.Sprintf(format, a...)
}

func (monochrome) Enabled() bool {
	return false
}

// EnableColor represents whether or not to return colored strings.
type EnableColor int

const (
	ColorNever EnableColor = iota
	ColorAuto
	ColorAlways
)

func isColorAvailable() bool {
	term := os.Getenv("TERM")
	switch term {
	case "dumb", "":
		return false
	}
	return isatty.IsTerminal()
}

// NewColor returns a color or monochrome type depending on the value of enableColor.
// A monochrome type will always return the string in the default color.
func NewColor(enableColor EnableColor) Color {
	ec := enableColor != ColorNever
	if enableColor == ColorAuto {
		ec = isColorAvailable()
	}
	if ec {
		return color{}
	} else {
		return monochrome{}
	}
}

// String returns the string value of the EnableColor type.
func (ec *EnableColor) String() string {
	switch *ec {
	case ColorNever:
		return "never"
	case ColorAuto:
		return "auto"
	case ColorAlways:
		return "always"
	}
	return ""
}

// Set sets the EnableColor type based on the string value.
func (ec *EnableColor) Set(s string) error {
	switch s {
	case "never":
		*ec = ColorNever
	case "auto":
		*ec = ColorAuto
	case "always":
		*ec = ColorAlways
	default:
		return fmt.Errorf("%s is not a valid color value", s)
	}
	return nil
}
