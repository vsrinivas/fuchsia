// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package direntry

import (
	"bytes"
	"errors"
	"strings"
	"testing"
)

func TestDOSToUnix(t *testing.T) {
	checkConvertDOSToUnix := func(b []byte, lower bool, goldBytes []byte) {
		name := convertDOSToUnix(b, lower)
		if !bytes.Equal([]byte(name), goldBytes) {
			t.Fatal("Unexpected name: ", name, ", expected: ", string(goldBytes), " from ", b)
		}
	}

	checkConvertDOSToUnix([]byte("FOOBAR  TXT"), false, []byte("FOOBAR.TXT"))   // Test simple case
	checkConvertDOSToUnix([]byte("foobar  txt"), false, []byte("FOOBAR.TXT"))   // Test lowercase DOS (invalid DOS string)
	checkConvertDOSToUnix([]byte("FOOBAR  TXT"), true, []byte("foobar.txt"))    // Test lowercase Unix
	checkConvertDOSToUnix([]byte("FOO.BAR TXT"), false, []byte("FOO?BAR.TXT"))  // Test embedded '.'
	checkConvertDOSToUnix([]byte("FOO        "), false, []byte("FOO"))          // Test without extension
	checkConvertDOSToUnix([]byte("..FOO      "), false, []byte("??FOO"))        // Test leading '.'
	checkConvertDOSToUnix([]byte("/FOO       "), false, []byte("?FOO"))         // Test slash
	checkConvertDOSToUnix([]byte("F       T  "), false, []byte("F.T"))          // Test small name
	checkConvertDOSToUnix([]byte("F          "), false, []byte("F"))            // Test small name (no extension)
	checkConvertDOSToUnix([]byte("FOOTBARTPNG"), false, []byte("FOOTBART.PNG")) // Test full name
	checkConvertDOSToUnix([]byte("FOOBAR~1   "), false, []byte("FOOBAR~1"))     // Test name with gen number
	checkConvertDOSToUnix([]byte("BASHRC~1SWP"), false, []byte("BASHRC~1.SWP")) // Test name with gen number and extension
	checkConvertDOSToUnix([]byte(".          "), false, []byte("."))            // Test '.' special case
	checkConvertDOSToUnix([]byte("..         "), false, []byte(".."))           // Test '..' special case

	cp850inputBytesUpper := []byte{0x80, 0x90, 0xB5, 0xB0, 0xC0, 0xD1, 0x20, 0x20, 0xFE, 0x20, 0x20}
	cp850inputBytesLower := []byte{0x87, 0x82, 0xA0, 0xB0, 0xC0, 0xD0, 0x20, 0x20, 0xFE, 0x20, 0x20}
	checkConvertDOSToUnix(cp850inputBytesUpper, false, []byte("ÇÉÁ░└Ð.■")) // Test valid use of code page 850 (upper --> upper)
	checkConvertDOSToUnix(cp850inputBytesUpper, true, []byte("çéá░└ð.■"))  // Test valid use of code page 850 (upper --> lower)
	checkConvertDOSToUnix(cp850inputBytesLower, true, []byte("çéá░└ð.■"))  // Test valid use of code page 850 (lower --> lower)
	checkConvertDOSToUnix(cp850inputBytesLower, false, []byte("ÇÉÁ░└Ð.■")) // Test valid use of code page 850 (lower --> upper)

	// Test the "slot E5" special case
	// "0x05" in direntryName[0] means the character should be "0xE5"
	// When translating from cp850 to Unicode, 0xE5 gets mapped to 0x00D5, which is 'Õ'
	checkConvertDOSToUnix(append([]byte{charE5}, []byte("ABCDEFGTXT")...), false, []byte("ÕABCDEFG.TXT"))
	// Test that this swap only happens for direntryName[0]
	checkConvertDOSToUnix(append([]byte{charE5, charE5, charE5}, []byte("CDEFGTXT")...), false, []byte("Õ??CDEFG.TXT"))
	checkConvertDOSToUnix(append([]byte{charFree}, []byte("ABCDEFGTXT")...), false, nil)     // Test charFree special case
	checkConvertDOSToUnix(append([]byte{charLastFree}, []byte("ABCDEFGTXT")...), false, nil) // Test charLastFree special case
}

func TestUnixToDOSShort(t *testing.T) {
	checkConvertUnixToDOS := func(nameUnix string, goldLongNeeded bool, goldName []byte) {
		if len(goldName) != dosNameLen {
			panic("Malformed test case; invalid gold DOS name length")
		}
		callback := func(i uint) ([]byte, error) {
			panic("The callback should not be necessary for this test case")
		}
		name, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		}
		if goldLongNeeded != longnameNeeded {
			t.Fatal("Expectation of longname necessity did not match test results")
		} else if !bytes.Equal(name, goldName) {
			t.Fatal("Unexpected name: ", name, ", expected: ", goldName, " from ", nameUnix)
		}
	}

	// Unix Filename --> Short Name
	checkConvertUnixToDOS("FOOBAR.TXT", false, []byte("FOOBAR  TXT"))   // Test capitalization (upper)
	checkConvertUnixToDOS("foobar.txt", true, []byte("FOOBAR  TXT"))    // Test capitalization (lower)
	checkConvertUnixToDOS("FooBar.TxT", true, []byte("FOOBAR  TXT"))    // Test capitalization (mixed)
	checkConvertUnixToDOS("FOOTBART.DOC", false, []byte("FOOTBARTDOC")) // Test max length
	checkConvertUnixToDOS("FootBart.png", true, []byte("FOOTBARTPNG"))  // Test max length and capitalization
	checkConvertUnixToDOS("f.t", true, []byte("F       T  "))           // Test short length (with extension)
	checkConvertUnixToDOS("f", true, []byte("F          "))             // Test short length (without extension)
	checkConvertUnixToDOS("F", false, []byte("F          "))            // Test short length (capitalized)
	checkConvertUnixToDOS("foobar", true, []byte("FOOBAR     "))        // Test without . or extension
	checkConvertUnixToDOS("foobar     ", true, []byte("FOOBAR     "))   // Test trailing spaces
	checkConvertUnixToDOS("foobar.", true, []byte("FOOBAR     "))       // Test without extention
	checkConvertUnixToDOS("foobar......", true, []byte("FOOBAR     "))  // Test trailing periods
	checkConvertUnixToDOS("foo.txt.", true, []byte("FOO     TXT"))      // Test embedded periods
	checkConvertUnixToDOS(".", false, []byte(".          "))            // Test special '.' case
	checkConvertUnixToDOS("..", false, []byte("..         "))           // Test special '..' case

	// Test code page 850 (used by FAT)
	checkConvertUnixToDOS("ÁÇÆ╬¥Ò", false, append([]byte{0xB5, 0x80, 0x92, 0xCE, 0xBE, 0xE3}, []byte("     ")...))
	checkConvertUnixToDOS("áçæ╬¥ò", true, append([]byte{0xB5, 0x80, 0x92, 0xCE, 0xBE, 0xE3}, []byte("     ")...))

	// Test the "slot E5" special case
	checkConvertUnixToDOS("ÕABCDEFG.TXT", false, append([]byte{0x05}, []byte("ABCDEFGTXT")...))
	// Test that this case only applies to direntryName[0]
	checkConvertUnixToDOS("ÕÕÕCDEFG.TXT", false, append([]byte{0x05, 0xE5, 0xE5}, []byte("CDEFGTXT")...))
}

func TestUnixToDOSLong(t *testing.T) {
	directoryContents := [][]byte{LastFreeDirent()}
	checkConvertUnixToDOS := func(nameUnix string, goldName []byte) {
		if len(goldName) != dosNameLen {
			panic("Malformed test case; invalid gold DOS name length")
		}
		callback := func(i uint) ([]byte, error) {
			return directoryContents[i], nil
		}
		name, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		}
		if longnameNeeded != true {
			t.Fatal("Expectation of longname necessity did not match test results")
		} else if !bytes.Equal(name, goldName) {
			t.Fatal("Unexpected name: ", name, ", expected: ", goldName, " from ", nameUnix)
		}
	}

	// Unix Filename --> Long Name
	checkConvertUnixToDOS(".bashrc.swp", []byte("BASHRC~1SWP"))         // Test leading '.' with extension
	checkConvertUnixToDOS("foo.bar.txt", []byte("FOOBAR~1TXT"))         // Test embedded '.'
	checkConvertUnixToDOS("fo..ba..tx.", []byte("FOBA~1  TX "))         // Test multiple embedded '.'
	checkConvertUnixToDOS("fo..........o", []byte("FO~1    O  "))       // Test multiple embedded '.' (next to extension)
	checkConvertUnixToDOS("footbartpng", []byte("FOOTBA~1   "))         // Test 8.3 name without '.'
	checkConvertUnixToDOS("TextFile.Mine.txt", []byte("TEXTFI~1TXT"))   // Test simple case
	checkConvertUnixToDOS("ver 12.txt", []byte("VER12~1 TXT"))          // Test embedded space
	checkConvertUnixToDOS("ver      12.txt", []byte("VER12~1 TXT"))     // Test multiple embedded spaces
	checkConvertUnixToDOS("ver +1.2.text", []byte("VER_12~1TEX"))       // Test disallowed char "+" in base name
	checkConvertUnixToDOS("ver 1.2.t+t", []byte("VER12~1 T_T"))         // Test disallowed char "+" in extension
	checkConvertUnixToDOS("ver +1.2.t+t", []byte("VER_12~1T_T"))        // Test disallowed char "+" in both parts of name
	checkConvertUnixToDOS("a,b;c=", []byte("A_B_C_~1   "))              // Test disallowed chars ",;="
	checkConvertUnixToDOS("a+b[c]", []byte("A_B_C_~1   "))              // Test disallowed chars "+[]"
	checkConvertUnixToDOS("AƑBƔCỠ.txt", []byte("A_B_C_~1TXT"))          // Test unicode chars (NOT from page 850)
	checkConvertUnixToDOS("ThisIsALongFile.txt", []byte("THISIS~1TXT")) // Test long name
	checkConvertUnixToDOS("The quick brown.fox", []byte("THEQUI~1FOX")) // Test long name with spaces
	checkConvertUnixToDOS("The quick 棕色.狐狸!", []byte("THEQUI~1__!"))    // Test long name with spaces, embedded '.', unicode

	// Utility for making a fake directory
	appendEntry := func(name string) {
		short := &shortDirentry{}
		short.setName([]byte(name))
		directoryContents = append(directoryContents, short.bytes())
	}

	// Test generation number in a fake directory
	directoryContents = make([][]byte, 0)
	appendEntry(".          ")
	appendEntry("..         ")
	directoryContents = append(directoryContents, LastFreeDirent())
	checkConvertUnixToDOS(".foobar", []byte("FOOBAR~1   "))

	// Test a generation number which is already occupied
	directoryContents = make([][]byte, 0)
	appendEntry(".          ")
	appendEntry("..         ")
	appendEntry("FOOBAR~1   ")
	directoryContents = append(directoryContents, LastFreeDirent())
	checkConvertUnixToDOS(".foobar", []byte("FOOBAR~2   "))

	// Test larger generatation number
	directoryContents = make([][]byte, 0)
	appendEntry(".          ")
	appendEntry("..         ")
	appendEntry("FOOBAR~1   ")
	appendEntry("FOOBAR~2   ")
	appendEntry("FOOBAR~3   ")
	appendEntry("NOTFOO~4   ") // Not "FOOBAR~4" intentionally
	appendEntry("FOOBAR~5   ")
	directoryContents = append(directoryContents, LastFreeDirent())
	checkConvertUnixToDOS(".foobar", []byte("FOOBAR~4   "))

	// Test the same generation number, but skipping over a free spot
	directoryContents = make([][]byte, 0)
	appendEntry(".          ")
	appendEntry("..         ")
	appendEntry("FOOBAR~1   ")
	appendEntry("FOOBAR~2   ")
	appendEntry("FOOBAR~3   ")
	directoryContents = append(directoryContents, FreeDirent())
	appendEntry("FOOBAR~5   ")
	directoryContents = append(directoryContents, LastFreeDirent())
	checkConvertUnixToDOS(".foobar", []byte("FOOBAR~4   "))

	// Test multi-digit generation number
	directoryContents = make([][]byte, 0)
	appendEntry(".          ")
	appendEntry("..         ")
	appendEntry("FOOBAR~1   ")
	appendEntry("FOOBAR~2   ")
	appendEntry("FOOBAR~3   ")
	appendEntry("FOOBAR~4   ")
	appendEntry("FOOBAR~5   ")
	appendEntry("FOOBAR~6   ")
	appendEntry("FOOBAR~7   ")
	appendEntry("FOOBAR~8   ")
	appendEntry("FOOBAR~9   ")
	directoryContents = append(directoryContents, LastFreeDirent())
	checkConvertUnixToDOS(".foobar", []byte("FOOBA~10   "))

	// TODO(smklein): I'm actually not sure if this is the right behavior. Should the "~9" be at the
	// right side of the "main" DOS name?
}

func TestUnixToDOSFailure(t *testing.T) {
	checkConvertUnixToDOSFail := func(nameUnix string, goldErr error) {
		callback := func(i uint) ([]byte, error) {
			panic("The callback should not be necessary for this test case")
		}
		_, _, err := convertUnixToDOS(callback, nameUnix)
		if err != goldErr {
			t.Fatalf("Expected error %s, but saw %s", goldErr, err)
		}
	}

	// Unix Filename --> Failure case
	checkConvertUnixToDOSFail("...", errInvalidFilename) // Test disallowing "only dots and spaces"
	checkConvertUnixToDOSFail(".  ", errInvalidFilename)
	checkConvertUnixToDOSFail(" . ", errInvalidFilename)
	checkConvertUnixToDOSFail("  .", errInvalidFilename)
	checkConvertUnixToDOSFail("", errInvalidFilename)      // Test empty string
	checkConvertUnixToDOSFail("\000", errInvalidFilename)  // Test NULL character
	checkConvertUnixToDOSFail("foo\"", errInvalidFilename) // " character is disallowed
	checkConvertUnixToDOSFail("foo?", errInvalidFilename)  // ? character is disallowed
	checkConvertUnixToDOSFail("foo/", errInvalidFilename)  // / character is disallowed
	checkConvertUnixToDOSFail("foo*", errInvalidFilename)  // * character is disallowed
}

func TestUnixDOSCombo(t *testing.T) {
	checkConvertUnixToDOSAndBack := func(nameUnix string, lowercase bool) {
		callback := func(i uint) ([]byte, error) {
			panic("The callback should not be necessary for this test case")
		}
		nameDOS, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		} else if !lowercase && longnameNeeded {
			t.Fatal("Expected to not need a longname")
		}
		name := convertDOSToUnix(nameDOS, lowercase)
		if name != nameUnix {
			t.Fatal("Unexpected name: ", name, ", expected: ", nameUnix)
		}
	}

	checkConvertUnixToDOSAndBack("foobar.txt", true)    // Test simple case
	checkConvertUnixToDOSAndBack("footbart.doc", true)  // Test max length
	checkConvertUnixToDOSAndBack("f.t", true)           // Test short length (with extension)
	checkConvertUnixToDOSAndBack("f", true)             // Test short length
	checkConvertUnixToDOSAndBack("foobar", true)        // Test without . or extension
	checkConvertUnixToDOSAndBack(".", true)             // Test special '.' case
	checkConvertUnixToDOSAndBack("..", true)            // Test special '..' case
	checkConvertUnixToDOSAndBack("áçæ╬¥ò", true)        // Test code page 850 (lowercase)
	checkConvertUnixToDOSAndBack("ÁÇÆ╬¥Ò", false)       // Test code page 850 (uppercase)
	checkConvertUnixToDOSAndBack("ÕABCDEFG.TXT", false) // Test the "charE5" special case
	checkConvertUnixToDOSAndBack("ÕÕÕCDEFG.TXT", false) // Test that this case only applies to direntryName[0]
}

func TestConvertUnixToWin(t *testing.T) {
	checkConvertUnixToWin := func(nameUnix string, expectedNames [][]uint16) {
		// Create "nameDOS"
		directoryContents := [][]byte{
			LastFreeDirent(),
		}
		callback := func(i uint) ([]byte, error) {
			return directoryContents[i], nil
		}
		nameDOS, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		}
		if !longnameNeeded {
			t.Fatal("Expected that generation number would be inserted for a long name")
		}

		// Create long direntries
		longDirentries, err := convertUnixToWin(nameUnix, nameDOS)
		if err != nil {
			t.Fatal(err)
		}

		// Validate length
		expectedLength := len(expectedNames)
		if len(longDirentries) != expectedLength {
			t.Fatalf("Expected %d long direntries, but saw %d", expectedLength, len(longDirentries))
		}

		order := uint8(0)
		for i := range longDirentries {
			// Validate the "Last Long Entry" bit and order
			if i == 0 {
				order = longDirentries[i].count & longOrdinalMask
				if longDirentries[i].count&longLastEntry == 0 {
					t.Fatal("The Last Entry marker should appear first in the list of Long Direntries")
				}
			} else {
				if longDirentries[i].count&longLastEntry != 0 {
					t.Fatal("The Last Entry marker should NOT appear in subsequent direntries")
				}

				newOrder := longDirentries[i].count & longOrdinalMask
				if newOrder != order-1 {
					t.Fatal("Invalid direntry order")
				}
				order = newOrder
			}

			// Validate name
			rawName := longDirentries[i].nameRaw()
			expectedRaw := expectedNames[i]
			if len(rawName) != len(expectedRaw) {
				t.Fatalf("Unexpected name length %d (expected %d)", len(rawName), len(expectedRaw))
			}
			for j := range rawName {
				if rawName[j] != expectedRaw[j] {
					t.Fatalf("Unexpected values: rawName[%d]: %x, expectedRaw[%d]: %x", j, rawName[j], j, expectedRaw[j])
				}
			}
		}
	}

	// Tests simple, single-direntry case (with NULL + padding)
	expectedNames := make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x002E, 0x0066, 0x006F, 0x006F, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	checkConvertUnixToWin(".foo", expectedNames)

	// Test case sensitivity
	expectedNames = make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x002E, 0x0046, 0x006F, 0x004F, 0x0062, 0x0041, 0x0072, 0x0000,
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	checkConvertUnixToWin(".FoObAr", expectedNames)

	// Tests single-direntry case (with NULL, but no padding)
	expectedNames = make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x002E, 0x0066, 0x006F, 0x006F, 0x0062, 0x0061, 0x0072, 0x0062,
		0x0061, 0x007A, 0x0062, 0x006F, 0x0000,
	}
	checkConvertUnixToWin(".foobarbazbo", expectedNames)

	// Tests single-direntry case (with no NULL and no padding)
	expectedNames = make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x002E, 0x0066, 0x006F, 0x006F, 0x0062, 0x0061, 0x0072, 0x0062,
		0x0061, 0x007A, 0x0062, 0x006F, 0x0074,
	}
	checkConvertUnixToWin(".foobarbazbot", expectedNames)

	// Tests example from "FAT: General Overview of On-Disk Format"
	expectedNames = make([][]uint16, 2)
	expectedNames[0] = []uint16{ // "wn.fox"
		0x0077, 0x006E, 0x002E, 0x0066, 0x006F, 0x0078, 0x0000, 0xFFFF,
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	expectedNames[1] = []uint16{ // "The quick bro"
		0x0054, 0x0068, 0x0065, 0x0020, 0x0071, 0x0075, 0x0069, 0x0063,
		0x006B, 0x0020, 0x0062, 0x0072, 0x006F,
	}
	checkConvertUnixToWin("The quick brown.fox", expectedNames)

	// Tests longer direntry name
	expectedNames = make([][]uint16, 5)
	expectedNames[0] = []uint16{ // "hat a story!"
		0x0068, 0x0061, 0x0074, 0x0020, 0x0061, 0x0020, 0x0073, 0x0074,
		0x006F, 0x0072, 0x0079, 0x0021, 0x0000,
	}
	expectedNames[1] = []uint16{ // "y dog! Wow, w"
		0x0079, 0x0020, 0x0064, 0x006F, 0x0067, 0x0021, 0x0020, 0x0057,
		0x006F, 0x0077, 0x002C, 0x0020, 0x0077,
	}
	expectedNames[2] = []uint16{ // " over the laz"
		0x0020, 0x006F, 0x0076, 0x0065, 0x0072, 0x0020, 0x0074, 0x0068,
		0x0065, 0x0020, 0x006c, 0x0061, 0x007A,
	}
	expectedNames[3] = []uint16{ // "wn fox jumped"
		0x0077, 0x006E, 0x0020, 0x0066, 0x006F, 0x0078, 0x0020, 0x006A,
		0x0075, 0x006D, 0x0070, 0x0065, 0x0064,
	}
	expectedNames[4] = []uint16{ // "The quick bro"
		0x0054, 0x0068, 0x0065, 0x0020, 0x0071, 0x0075, 0x0069, 0x0063,
		0x006B, 0x0020, 0x0062, 0x0072, 0x006F,
	}
	checkConvertUnixToWin("The quick brown fox jumped over the lazy dog! Wow, what a story!", expectedNames)

	// Tests longest direntry name
	expectedNames = make([][]uint16, 20)
	expectedNames[0] = []uint16{
		0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061,
		0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	for i := 1; i < 20; i++ {
		expectedNames[i] = []uint16{
			0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061, 0x0061,
			0x0061, 0x0061, 0x0061, 0x0061, 0x0061,
		}
	}
	checkConvertUnixToWin(strings.Repeat("a", 255), expectedNames)

	// Tests use of characters which are disallowed in short filenames
	expectedNames = make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x002B, 0x002C, 0x003B, 0x003D, 0x005B, 0x005D, 0x0000, 0xFFFF,
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	checkConvertUnixToWin("+,;=[]", expectedNames)

	// Tests valid UCS-16 unicode
	expectedNames = make([][]uint16, 1)
	expectedNames[0] = []uint16{
		0x0191, 0x0194, 0x1EE0, 0x68D5, 0x8272, 0x0000, 0xFFFF, 0xFFFF,
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	}
	checkConvertUnixToWin("ƑƔỠ棕色", expectedNames)
}

func TestConvertUnixToWinFailure(t *testing.T) {
	checkConvertUnixToWinFailure := func(nameUnix string, goldErr error) {
		// Create "nameDOS".
		directoryContents := [][]byte{
			LastFreeDirent(),
		}
		callback := func(i uint) ([]byte, error) {
			return directoryContents[i], nil
		}
		nameDOS, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		}
		if !longnameNeeded {
			t.Fatal("Expected that a long name would be necessary for this test case")
		}

		// Create long direntries
		_, err = convertUnixToWin(nameUnix, nameDOS)
		if err != goldErr {
			t.Fatalf("Expected error %s, but saw %s", goldErr, err)
		}
	}

	// Tests one longer than the longest direntry name
	checkConvertUnixToWinFailure(strings.Repeat("a", 256), errTooLong)
}

func TestGetShortEntryFromWin(t *testing.T) {
	checkGetShortEntryFromWin := func(buf, goldShortBuf []byte, goldNumDirentrySlots uint8) {
		callback := func(i uint) ([]byte, error) {
			return buf[i*DirentrySize : (i+1)*DirentrySize], nil
		}
		shortEntry, numDirentrySlots, err := getShortEntryFromWin(callback, 0)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(shortEntry, goldShortBuf) {
			t.Fatal("Invalid short entry read from LFN")
		} else if numDirentrySlots != goldNumDirentrySlots {
			t.Fatalf("Unexpected number of direntry slots %d (expected %d)", numDirentrySlots, goldNumDirentrySlots)
		}
	}

	// Test order == 1
	long := make([]longDirentry, 1)
	long[0].count = longLastEntry | 1
	short := &shortDirentry{}
	short.setName([]byte("FOOBAR     "))
	buf := append(long[0].bytes(), short.bytes()...)
	checkGetShortEntryFromWin(buf, short.bytes(), 2)

	// Test order == 2
	long = make([]longDirentry, 2)
	long[0].count = longLastEntry | 2
	long[1].count = 1
	short = &shortDirentry{}
	short.setName([]byte("FOOBAR  BAZ"))
	buf = append(long[0].bytes(), long[1].bytes()...)
	buf = append(buf, short.bytes()...)
	checkGetShortEntryFromWin(buf, short.bytes(), 3)
}

func TestGetShortEntryFromWinFailure(t *testing.T) {
	// Test completely broken callback
	callbackErr := errors.New("This callback is broken")
	callbackBad := func(i uint) ([]byte, error) {
		return nil, callbackErr
	}
	_, _, err := getShortEntryFromWin(callbackBad, 0)
	if err != callbackErr {
		t.Fatal("Expected callback error")
	}

	// Test valid callback, but invalid longLastEntry (implying an invalid sequence)
	long := make([]longDirentry, 2)
	long[0].count = 2
	long[1].count = 1 | longLastEntry
	short := &shortDirentry{}
	short.setName([]byte("FOOBAR  BAZ"))
	buf := append(long[0].bytes(), long[1].bytes()...)
	buf = append(buf, short.bytes()...)
	callbackGood := func(i uint) ([]byte, error) {
		return buf[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	_, _, err = getShortEntryFromWin(callbackGood, 0)
	if err != errLongDirentry {
		t.Fatalf("Expected error %s, saw %s", errLongDirentry, err)
	}

	// Test valid callback, but invalid order
	long = make([]longDirentry, 2)
	long[0].count = 0 | longLastEntry
	long[1].count = 1
	short = &shortDirentry{}
	short.setName([]byte("FOOBAR  BAZ"))
	buf = append(long[0].bytes(), long[1].bytes()...)
	buf = append(buf, short.bytes()...)
	callbackGood = func(i uint) ([]byte, error) {
		return buf[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	_, _, err = getShortEntryFromWin(callbackGood, 0)
	if err != errLongDirentry {
		t.Fatalf("Expected error %s, saw %s", errLongDirentry, err)
	}

	// Test partially broken callback, which can access the first long direntry, but not the short
	// entry.
	long = make([]longDirentry, 2)
	long[0].count = 2 | longLastEntry
	long[1].count = 1
	short = &shortDirentry{}
	short.setName([]byte("FOOBAR  BAZ"))
	buf = append(long[0].bytes(), long[1].bytes()...)
	buf = append(buf, short.bytes()...)
	callbackGood = func(i uint) ([]byte, error) {
		if i == 0 {
			return buf[i*DirentrySize : (i+1)*DirentrySize], nil
		}
		return nil, callbackErr
	}
	_, _, err = getShortEntryFromWin(callbackGood, 0)
	if err != callbackErr {
		t.Fatal("Expected callback err")
	}
}

func TestConvertWinToUnix(t *testing.T) {
	checkConvertWinToUnix := func(direntries []longDirentry, goldNameUnix string) {
		callback := func(i uint) ([]byte, error) {
			if i < uint(len(direntries)) {
				return direntries[i].bytes(), nil
			}
			return LastFreeDirent(), nil
		}

		nameDOS, _, err := convertUnixToDOS(callback, goldNameUnix)
		if err != nil {
			t.Fatal(err)
		}
		chksum := checksum(nameDOS)
		for i := range direntries {
			direntries[i].chksum = chksum
		}

		nameUnix, err := convertWinToUnix(callback, 0, chksum, uint8(len(direntries)))
		if err != nil {
			t.Fatal(err)
		} else if nameUnix != goldNameUnix {
			t.Fatalf("Unexpected unix name %s (expected %s)", nameUnix, goldNameUnix)
		}
	}

	// Test simple case (terminator in part 1 of name)
	direntries := make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	copy(direntries[0].name1[:], []uint8{0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x00, 0x00})
	copy(direntries[0].name2[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	checkConvertWinToUnix(direntries, ".foo")

	// Test simple case (terminator in part 2 of name)
	direntries = make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	copy(direntries[0].name1[:], []uint8{0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x6F, 0x00})
	copy(direntries[0].name2[:], []uint8{0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	checkConvertWinToUnix(direntries, ".fooo")

	// Test simple case (terminator in part 3 of name)
	direntries = make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	copy(direntries[0].name1[:], []uint8{0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x6F, 0x00})
	copy(direntries[0].name2[:], []uint8{0x6F, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x2E, 0x00})
	copy(direntries[0].name3[:], []uint8{0x66, 0x00, 0x00, 0x00})
	checkConvertWinToUnix(direntries, ".foooooooo.f")

	// Tests example from "FAT: General Overview of On-Disk Format"
	direntries = make([]longDirentry, 2)
	direntries[0].count = longLastEntry | 2
	copy(direntries[0].name1[:], []uint8{0x77, 0x00, 0x6E, 0x00, 0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00})
	copy(direntries[0].name2[:], []uint8{0x78, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	direntries[1].count = 1
	copy(direntries[1].name1[:], []uint8{0x54, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x71, 0x00})
	copy(direntries[1].name2[:], []uint8{0x75, 0x00, 0x69, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x20, 0x00, 0x62, 0x00})
	copy(direntries[1].name3[:], []uint8{0x72, 0x00, 0x6F, 0x00})
	checkConvertWinToUnix(direntries, "The quick brown.fox")

	// Test unicode case
	direntries = make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	copy(direntries[0].name1[:], []uint8{0x91, 0x01, 0x94, 0x01, 0xE0, 0x1E, 0xD5, 0x68, 0x72, 0x82})
	copy(direntries[0].name2[:], []uint8{0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	checkConvertWinToUnix(direntries, "ƑƔỠ棕色")

	// Tests longest direntry name
	direntries = make([]longDirentry, 20)
	direntries[0].count = longLastEntry | 20
	copy(direntries[0].name1[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
	copy(direntries[0].name2[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	for i := 1; i < len(direntries); i++ {
		direntries[i].count = uint8(len(direntries) - i)
		copy(direntries[i].name1[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
		copy(direntries[i].name2[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
		copy(direntries[i].name3[:], []uint8{0x61, 0x00, 0x61, 0x00})
	}
	checkConvertWinToUnix(direntries, strings.Repeat("a", 255))
}

func TestConvertWinToUnixFailure(t *testing.T) {
	applyChecksum := func(direntries []longDirentry, chksum uint8) {
		for i := range direntries {
			direntries[i].chksum = chksum
		}
	}

	checkConvertWinToUnixFailure := func(direntries []longDirentry, chksum uint8, goldErr error) {
		callback := func(i uint) ([]byte, error) {
			return direntries[i].bytes(), nil
		}
		_, err := convertWinToUnix(callback, 0, chksum, uint8(len(direntries)))
		if err != goldErr {
			t.Fatalf("Expected error %s, saw error %s", goldErr, err)
		}
	}

	// Test direntry with a name that is one character too long
	direntries := make([]longDirentry, 20)
	direntries[0].count = longLastEntry | 20
	copy(direntries[0].name1[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
	copy(direntries[0].name2[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x00, 0x00, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	for i := 1; i < len(direntries); i++ {
		direntries[i].count = uint8(len(direntries) - i)
		copy(direntries[i].name1[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
		copy(direntries[i].name2[:], []uint8{0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00, 0x61, 0x00})
		copy(direntries[i].name3[:], []uint8{0x61, 0x00, 0x61, 0x00})
	}
	chksum := uint8(5)
	applyChecksum(direntries, chksum)
	checkConvertWinToUnixFailure(direntries, chksum, errTooLong)

	// Test variations of invalid order for a single long direntry
	direntries = make([]longDirentry, 1)
	copy(direntries[0].name1[:], []uint8{0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x00, 0x00})
	copy(direntries[0].name2[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	chksum = uint8(5)
	applyChecksum(direntries, chksum)
	// Order too small
	direntries[0].count = 0 | longLastEntry
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)
	// Order too large
	direntries[0].count = maxLongDirentries + 1 | longLastEntry
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)
	// "Last entry" not first
	direntries[0].count = 1
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)

	// Test variations of invalid order for a multiple long direntries
	direntries = make([]longDirentry, 2)
	copy(direntries[0].name1[:], []uint8{0x77, 0x00, 0x6E, 0x00, 0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00})
	copy(direntries[0].name2[:], []uint8{0x78, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[1].name1[:], []uint8{0x54, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x71, 0x00})
	copy(direntries[1].name2[:], []uint8{0x75, 0x00, 0x69, 0x00, 0x63, 0x00, 0x6B, 0x00, 0x20, 0x00, 0x62, 0x00})
	copy(direntries[1].name3[:], []uint8{0x72, 0x00, 0x6F, 0x00})
	chksum = uint8(5)
	applyChecksum(direntries, chksum)
	// Multiple "last entries"
	direntries[0].count = longLastEntry | 2
	direntries[1].count = longLastEntry | 1
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)
	// Last entry not first
	direntries[0].count = 2
	direntries[1].count = longLastEntry | 1
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)
	// NULL terminator seen early
	direntries[0].count = longLastEntry | 2
	direntries[1].count = 1
	copy(direntries[1].name3[:], []uint8{0x00, 0x00, 0x6F, 0x00})
	checkConvertWinToUnixFailure(direntries, chksum, errLongDirentry)

	// Test broken checksum
	direntries = make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	copy(direntries[0].name1[:], []uint8{0x2E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x00, 0x00})
	copy(direntries[0].name2[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
	copy(direntries[0].name3[:], []uint8{0xFF, 0xFF, 0xFF, 0xFF})
	chksum = uint8(5)
	applyChecksum(direntries, chksum+1) // Apply the broken checksum
	checkConvertWinToUnixFailure(direntries, chksum, errChecksum)

	// Test direntries which use the '/' character (0x002F), which is invalid for unix filenames
	direntries = make([]longDirentry, 1)
	direntries[0].count = longLastEntry | 1
	chksum = uint8(5)
	applyChecksum(direntries, chksum)
	copy(direntries[0].name1[:], []uint8{0x2F, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00}) // '/' here
	copy(direntries[0].name2[:], []uint8{0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00})
	copy(direntries[0].name3[:], []uint8{0x55, 0x00, 0x00, 0x00})
	checkConvertWinToUnixFailure(direntries, chksum, errInvalidFilename)
	copy(direntries[0].name1[:], []uint8{0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00})
	copy(direntries[0].name2[:], []uint8{0x55, 0x00, 0x2F, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00}) // '/' here
	copy(direntries[0].name3[:], []uint8{0x55, 0x00, 0x00, 0x00})
	checkConvertWinToUnixFailure(direntries, chksum, errInvalidFilename)
	copy(direntries[0].name1[:], []uint8{0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00})
	copy(direntries[0].name2[:], []uint8{0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00})
	copy(direntries[0].name3[:], []uint8{0x2F, 0x00, 0x00, 0x00}) // '/' here
	checkConvertWinToUnixFailure(direntries, chksum, errInvalidFilename)
}

// Convert a Unix name to a DOS name and a long name. Afterwards, convert back to the original Unix
// name. Verify that the new name matches the original.
func TestUnixWinCombo(t *testing.T) {
	checkUnixToWinAndBack := func(nameUnix string) {
		directoryContents := [][]byte{
			LastFreeDirent(),
		}
		callback := func(i uint) ([]byte, error) {
			return directoryContents[i], nil
		}

		nameDOS, longnameNeeded, err := convertUnixToDOS(callback, nameUnix)
		if err != nil {
			t.Fatal(err)
		} else if !longnameNeeded {
			t.Fatalf("%s was converted to a short name, but not a long name", nameUnix)
		}
		direntries, err := convertUnixToWin(nameUnix, nameDOS)
		if err != nil {
			t.Fatal(err)
		}
		callback = func(i uint) ([]byte, error) {
			return direntries[i].bytes(), nil
		}
		newNameUnix, err := convertWinToUnix(callback, 0, checksum(nameDOS), uint8(len(direntries)))
		if err != nil {
			t.Fatal(err)
		}
		if newNameUnix != nameUnix {
			t.Fatalf("Input name %s did not match output name %s", nameUnix, newNameUnix)
		}
	}

	checkUnixToWinAndBack("foo")                 // Test lowercase
	checkUnixToWinAndBack("FoObAr")              // Test mixed case
	checkUnixToWinAndBack(".foo")                // Test leading '.' (lowercase)
	checkUnixToWinAndBack(".FOO")                // Test leading '.' (uppercase)
	checkUnixToWinAndBack("The quick brown.fox") // Test long name (mixed case)
	checkUnixToWinAndBack("THEQUICKBROWN.FOX")   // Test long name (uppercase)
	checkUnixToWinAndBack("ƑƔỠ棕色")               // Test unicode
	checkUnixToWinAndBack("本")                   // Test short unicode
	checkUnixToWinAndBack("李白《静夜思》床前明月光，疑是地上霜。举头望明月，低头思故乡。 - Thoughts in the Silent Night, by Li Bai")
	checkUnixToWinAndBack(strings.Repeat("a", 255))
}
