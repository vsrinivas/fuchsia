// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package direntry

import (
	"errors"
	"strconv"
	"strings"
	"unicode/utf16"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bits"
)

const (
	cBad = 0 // Characters disallowed in both short and long filenames.
	cRpl = 1 // Characters +,;=[] should be replaced by '_', and a generation number MUST be used.
	cSkp = 2 // '.' or ' '. Skip this character.

	dosBaseNameLen = 8
	dosExtNameLen  = 3
	dosNameLen     = dosBaseNameLen + dosExtNameLen

	maxASCII = '\u007F' // Maximum ASCII value
)

var (
	errChecksum        = errors.New("Invalid checksum")
	errTooLong         = errors.New("Name too long")
	errInvalidFilename = errors.New("Invalid filename cannot be converted")
	errLongDirentry    = errors.New("Invalid long direntry")
)

// Converts ASCII characters [0, 0x7F] to DOS "Code page 850" characters.
var asciiFilter = [128]byte{
	cBad, cBad, cBad, cBad, cBad, cBad, cBad, cBad, /* 00-07 */
	cBad, cBad, cBad, cBad, cBad, cBad, cBad, cBad, /* 08-0f */
	cBad, cBad, cBad, cBad, cBad, cBad, cBad, cBad, /* 10-17 */
	cBad, cBad, cBad, cBad, cBad, cBad, cBad, cBad, /* 18-1f */
	cSkp, 0x21, cBad, 0x23, 0x24, 0x25, 0x26, 0x27, /* 20-27 */
	0x28, 0x29, cBad, cRpl, cRpl, 0x2d, cSkp, cBad, /* 28-2f */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 30-37 */
	0x38, 0x39, cBad, cRpl, cBad, cRpl, cBad, cBad, /* 38-3f */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 40-47 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 48-4f */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 50-57 */
	0x58, 0x59, 0x5a, cRpl, cBad, cRpl, 0x5e, 0x5f, /* 58-5f */
	0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 60-67 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 68-6f */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 70-77 */
	0x58, 0x59, 0x5a, 0x7b, cBad, 0x7d, 0x7e, cBad, /* 78-7f */
}

// Converts some unicode characters to DOS "Code page 850" characters.
var unicodeToDos = map[rune]byte{
	'\u00C7': 0x80, '\u00FC': 0x81, '\u00E9': 0x82, '\u00E2': 0x83, '\u00E4': 0x84, '\u00E0': 0x85, '\u00E5': 0x86, '\u00E7': 0x87,
	'\u00EA': 0x88, '\u00EB': 0x89, '\u00E8': 0x8A, '\u00EF': 0x8B, '\u00EE': 0x8C, '\u00EC': 0x8D, '\u00C4': 0x8E, '\u00C5': 0x8F,
	'\u00C9': 0x90, '\u00E6': 0x91, '\u00C6': 0x92, '\u00F4': 0x93, '\u00F6': 0x94, '\u00F2': 0x95, '\u00FB': 0x96, '\u00F9': 0x97,
	'\u00FF': 0x98, '\u00D6': 0x99, '\u00DC': 0x9A, '\u00F8': 0x9B, '\u00A3': 0x9C, '\u00D8': 0x9D, '\u00D7': 0x9E, '\u0192': 0x9F,
	'\u00E1': 0xA0, '\u00ED': 0xA1, '\u00F3': 0xA2, '\u00FA': 0xA3, '\u00F1': 0xA4, '\u00D1': 0xA5, '\u00AA': 0xA6, '\u00BA': 0xA7,
	'\u00BF': 0xA8, '\u00AE': 0xA9, '\u00AC': 0xAA, '\u00BD': 0xAB, '\u00BC': 0xAC, '\u00A1': 0xAD, '\u00AB': 0xAE, '\u00BB': 0xAF,
	'\u2591': 0xB0, '\u2592': 0xB1, '\u2593': 0xB2, '\u2502': 0xB3, '\u2524': 0xB4, '\u00C1': 0xB5, '\u00C2': 0xB6, '\u00C0': 0xB7,
	'\u00A9': 0xB8, '\u2563': 0xB9, '\u2551': 0xBA, '\u2557': 0xBB, '\u255D': 0xBC, '\u00A2': 0xBD, '\u00A5': 0xBE, '\u2510': 0xBF,
	'\u2514': 0xC0, '\u2534': 0xC1, '\u252C': 0xC2, '\u251C': 0xC3, '\u2500': 0xC4, '\u253C': 0xC5, '\u00E3': 0xC6, '\u00C3': 0xC7,
	'\u255A': 0xC8, '\u2554': 0xC9, '\u2569': 0xCA, '\u2566': 0xCB, '\u2560': 0xCC, '\u2550': 0xCD, '\u256C': 0xCE, '\u00A4': 0xCF,
	'\u00F0': 0xD0, '\u00D0': 0xD1, '\u00CA': 0xD2, '\u00CB': 0xD3, '\u00C8': 0xD4, '\u0131': 0xD5, '\u00CD': 0xD6, '\u00CE': 0xD7,
	'\u00CF': 0xD8, '\u2518': 0xD9, '\u250C': 0xDA, '\u2588': 0xDB, '\u2584': 0xDC, '\u00A6': 0xDD, '\u00CC': 0xDE, '\u2580': 0xDF,
	'\u00D3': 0xE0, '\u00DF': 0xE1, '\u00D4': 0xE2, '\u00D2': 0xE3, '\u00F5': 0xE4, '\u00D5': 0xE5, '\u00B5': 0xE6, '\u00FE': 0xE7,
	'\u00DE': 0xE8, '\u00DA': 0xE9, '\u00DB': 0xEA, '\u00D9': 0xEB, '\u00FD': 0xEC, '\u00DD': 0xED, '\u00AF': 0xEE, '\u00B4': 0xEF,
	'\u00AD': 0xF0, '\u00B1': 0xF1, '\u2017': 0xF2, '\u00BE': 0xF3, '\u00B6': 0xF4, '\u00A7': 0xF5, '\u00F7': 0xF6, '\u00B8': 0xF7,
	'\u00B0': 0xF8, '\u00A8': 0xF9, '\u00B7': 0xFA, '\u00B9': 0xFB, '\u00B3': 0xFC, '\u00B2': 0xFD, '\u25A0': 0xFE, '\u00A0': 0xFF,
}

// Converts DOS "Code page 850" characters in range [0x80, 0xFF] to Unicode runes.
var dosToUnicode = [128]rune{
	'\u00C7', '\u00FC', '\u00E9', '\u00E2', '\u00E4', '\u00E0', '\u00E5', '\u00E7',
	'\u00EA', '\u00EB', '\u00E8', '\u00EF', '\u00EE', '\u00EC', '\u00C4', '\u00C5',
	'\u00C9', '\u00E6', '\u00C6', '\u00F4', '\u00F6', '\u00F2', '\u00FB', '\u00F9',
	'\u00FF', '\u00D6', '\u00DC', '\u00F8', '\u00A3', '\u00D8', '\u00D7', '\u0192',
	'\u00E1', '\u00ED', '\u00F3', '\u00FA', '\u00F1', '\u00D1', '\u00AA', '\u00BA',
	'\u00BF', '\u00AE', '\u00AC', '\u00BD', '\u00BC', '\u00A1', '\u00AB', '\u00BB',
	'\u2591', '\u2592', '\u2593', '\u2502', '\u2524', '\u00C1', '\u00C2', '\u00C0',
	'\u00A9', '\u2563', '\u2551', '\u2557', '\u255D', '\u00A2', '\u00A5', '\u2510',
	'\u2514', '\u2534', '\u252C', '\u251C', '\u2500', '\u253C', '\u00E3', '\u00C3',
	'\u255A', '\u2554', '\u2569', '\u2566', '\u2560', '\u2550', '\u256C', '\u00A4',
	'\u00F0', '\u00D0', '\u00CA', '\u00CB', '\u00C8', '\u0131', '\u00CD', '\u00CE',
	'\u00CF', '\u2518', '\u250C', '\u2588', '\u2584', '\u00A6', '\u00CC', '\u2580',
	'\u00D3', '\u00DF', '\u00D4', '\u00D2', '\u00F5', '\u00D5', '\u00B5', '\u00FE',
	'\u00DE', '\u00DA', '\u00DB', '\u00D9', '\u00FD', '\u00DD', '\u00AF', '\u00B4',
	'\u00AD', '\u00B1', '\u2017', '\u00BE', '\u00B6', '\u00A7', '\u00F7', '\u00B8',
	'\u00B0', '\u00A8', '\u00B7', '\u00B9', '\u00B3', '\u00B2', '\u25A0', '\u00A0',
}

// DOS filenames consist of:
// 1) The "name" part ("FOO" from "FOO.TXT"), which is 8 characters long.
// 2) The "extension part ("TXT" from "FOO.TXT"), which is 3 characters long.
//
// Both parts may contain trailing spaces if the filename component does not use the maximum
// allocated space.

// convertDOSToUnix converts a DOS filename (single byte, cp850 encoding) to a UTF-8 encoded string.
func convertDOSToUnix(nameDOS []byte, lowercase bool) string {
	if len(nameDOS) != dosNameLen {
		panic("Unexpected convertDOSToUnix name len: ")
	}

	switch string(nameDOS) {
	case ".          ":
		return "."
	case "..         ":
		return ".."
	}

	nameUnix := make([]rune, 0, dosNameLen+1)
	for i := 0; i < dosBaseNameLen && nameDOS[i] != ' '; i++ {
		charDOS := nameDOS[i]
		if i == 0 {
			switch charDOS {
			case charE5:
				// "If DIR_Name[0] == 0x05 (SLOT_E5), then the actual file name character for this
				// byte is 0xE5". 0xE5 is reserved to mean "free entry". DOS quirk.
				charDOS = 0xE5
			case charLastFree, charFree:
				// Free entries do not have filenames
				return ""
			}
		}
		nameUnix = append(nameUnix, charDOSToUnix(charDOS))
	}

	// Jump to the extension.
	extStartIndex := dosBaseNameLen
	if nameDOS[extStartIndex] != ' ' {
		// Add a '.' if an extension exists, then add the extension itself.
		nameUnix = append(nameUnix, '.')
		for i := extStartIndex; i < dosNameLen && nameDOS[i] != ' '; i++ {
			nameUnix = append(nameUnix, charDOSToUnix(nameDOS[i]))
		}
	}

	// By default, DOS strings are uppercase. Conditionally convert them to lowercase.
	if lowercase {
		return strings.ToLower(string(nameUnix))
	}
	// DOS strings should not be stored as anything other than uppercase, but just in case, convert
	// it to uppercase.
	return strings.ToUpper(string(nameUnix))
}

// convertUnixToDOS converts a unix filename to a DOS filename according to Win95 rules.
// This function implements the "Basis-Name Generation Algorithm" and "Numeric-Tail Generation
// Algorithm" described in the FAT documentation.
//
// The resulting filename may either be a short name (which should be paired with a long filename)
// a short name (which should not be paired with a long filename), or empty (due to an error).
func convertUnixToDOS(callback GetDirentryCallback, nameUnix string) (nameDOS []byte, longnameNeeded bool, err error) {
	// "The UNICODE name passed to the file system is converted to upper case."
	containsLowercase := false
	nameUnixUpper := strings.ToUpper(nameUnix)
	if nameUnix != nameUnixUpper {
		containsLowercase = true
	}
	nameUnix = nameUnixUpper

	nameBaseDOS := make([]byte, 0, dosBaseNameLen)
	nameExtDOS := make([]byte, 0, dosExtNameLen)

	// Wraps up both the "Main" and "Extension" parts of the name into one slice.
	composeDOSName := func() []byte {
		// Add spacing to main and ext.
		for len(nameBaseDOS) < dosBaseNameLen {
			nameBaseDOS = append(nameBaseDOS, ' ')
		}
		for len(nameExtDOS) < dosExtNameLen {
			nameExtDOS = append(nameExtDOS, ' ')
		}

		// This is a DOS-ism. 'E5' is reserved for charFree, but '05' means this byte should later
		// be replaced with 'E5'.
		if nameBaseDOS[0] == charFree {
			nameBaseDOS[0] = charE5
		}
		res := append(nameBaseDOS, nameExtDOS...)
		return res
	}

	// The filenames "." and ".." are handled specially, since they don't follow DOS filename rules.
	if string(nameUnix) == "." {
		nameBaseDOS = append(nameBaseDOS, '.')
		return composeDOSName(), false, nil
	}
	if string(nameUnix) == ".." {
		nameBaseDOS = append(nameBaseDOS, '.', '.')
		return composeDOSName(), false, nil
	}

	// Skip all trailing spaces and dots (they are ignored in both short and long names).
	nameUnix = strings.TrimRight(nameUnix, " .")
	if nameUnix == "" {
		// Filenames cannot contain EXCLUSIVELY spaces and dots.
		return nil, false, errInvalidFilename
	}

	// Convert the upper-case unicode name to cp850.
	nameDOSRaw := make([]byte, 0, len(nameUnix))
	// "lossyConversion" indicates that we may have left some characters behind that would have fit
	// in a long name.
	lossyConversion := false
	leadingPeriods := true
	for i, runeValue := range nameUnix {
		c := charUnixToDOS(runeValue)
		switch c {
		case cRpl:
			nameDOSRaw = append(nameDOSRaw, '_')
			leadingPeriods = false
			lossyConversion = true
		case cSkp:
			if nameUnix[i] == '.' {
				if leadingPeriods { // Should be stored in long names, but not short names.
					lossyConversion = true
				} else { // For now, insert all non-leading/trailing '.' chars. Only one will be used.
					nameDOSRaw = append(nameDOSRaw, '.')
				}
			} else {
				// We always skip spaces for short DOS names.
				lossyConversion = true
			}
		case cBad:
			return nil, false, errInvalidFilename
		default:
			// Copy the converted character.
			nameDOSRaw = append(nameDOSRaw, c)
			leadingPeriods = false
		}
	}

	// Find the last non-trailing '.' character (the extension dot).
	var extensionDot int
	for extensionDot = len(nameDOSRaw) - 1; extensionDot >= 0; extensionDot-- {
		if nameDOSRaw[extensionDot] == '.' {
			break
		}
	}
	// ExtensionDot will be "-1" if it does not exist.

	// Copy the 'base' part of the name.
	for i := range nameDOSRaw {
		if i == extensionDot {
			// Copy everything up to the extension.
			break
		}
		if i == dosBaseNameLen {
			// Only copy up to "dosBaseNameLen" characters.
			lossyConversion = true
			break
		}
		charDOS := nameDOSRaw[i]
		if charDOS == '.' {
			// We're losing an inner '.' character.
			lossyConversion = true
		} else {
			nameBaseDOS = append(nameBaseDOS, charDOS)
		}
	}

	// If the extension exists...
	if extensionDot != -1 {
		// ... copy the 'extension' part of the name.
		for i := extensionDot + 1; i < len(nameDOSRaw); i++ {
			if i == extensionDot+1+dosExtNameLen {
				// Long extension; only copy the first "dosExtNameLen" chars.
				lossyConversion = true
				break
			}
			nameExtDOS = append(nameExtDOS, nameDOSRaw[i])
		}
	}

	if !lossyConversion {
		// If there weren't any characters dropped, there is no room for generation numbers.
		// A long name is only necessary if there were lowercase characters.
		return composeDOSName(), containsLowercase, nil
	}

	maxGentextLen := 6
	for gen := 1; ; gen++ {
		gentext := strconv.Itoa(gen)
		if len(gentext) > maxGentextLen {
			// Given this filename, we have been unable to generate a corresponding short name.
			return nil, false, errInvalidFilename
		}
		// If the nameMain component is too big, truncate.
		tildeIndex := dosBaseNameLen - (len(gentext) + 1)
		if len(nameBaseDOS) > tildeIndex {
			nameBaseDOS = nameBaseDOS[:tildeIndex]
		}

		// The '~' is a special FAT character identifiying that the generator follows.
		nameBaseDOS = append(nameBaseDOS, '~')
		nameBaseDOS = append(nameBaseDOS, gentext[:]...)

		// Verify the short name does not currently exist -- if it does, try using a different
		// generation number.
		shortName := composeDOSName()
		if exists, err := doesShortNameExist(callback, shortName); err != nil {
			return nil, false, err
		} else if !exists {
			return shortName, true, nil
		}
	}
}

// convertUnixToWin converts a unix filename to a Win95 long name directory entry.
// This function also takes the short filename, which is used for checksumming purposes.
//
// Note: assumes that nameDOS is valid; assumes nameDOS was generated from nameUnix.
func convertUnixToWin(nameUnix string, nameDOS []byte) ([]longDirentry, error) {
	// "Leading and trailing spaces in a long name are ignored"
	nameUnix = strings.Trim(nameUnix, " ")
	// "Trailing periods are ignored"
	nameUnix = strings.TrimRight(nameUnix, ".")

	// Convert the UTF-8 Name to UCS-2 (the LFN format for FAT)
	bufUCS2 := make([]rune, 0, len(nameUnix)/2)
	for _, runeValue := range nameUnix {
		bufUCS2 = append(bufUCS2, runeValue)
	}
	nameUCS2 := utf16.Encode(bufUCS2)
	if longnameMaxLen < len(nameUCS2) {
		return nil, errTooLong
	}

	// Iterate through the long direntries slice backwards to match the on-disk layout
	longDirentries := make([]longDirentry, (len(nameUCS2)+longDirentLen-1)/longDirentLen)
	dirIndex := len(longDirentries)
	// Pre-calculate the checksum; it must be inserted into every long direntry component
	chksum := checksum(nameDOS)
	// True if the UCS2 name is finished being inserted into the collection of long direntries
	end := false

	for !end && len(nameUCS2) != 0 {
		dirIndex--
		longDirentries[dirIndex] = longDirentry{
			count:      uint8(len(longDirentries) - dirIndex),
			attributes: attrLongname,
			reserved1:  0,
			chksum:     uint8(chksum),
		}
		longDirentries[dirIndex].reserved2[0] = 0
		longDirentries[dirIndex].reserved2[1] = 0

		var nameUCS2Part []uint16
		if longDirentLen <= len(nameUCS2) {
			nameUCS2Part, nameUCS2 = nameUCS2[:longDirentLen], nameUCS2[longDirentLen:]
		} else {
			nameUCS2Part, nameUCS2 = nameUCS2, []uint16{}
		}

		// Now convert the filename parts
		convFilenamePart := func(inputPart []uint16, outputPart []uint8, end bool) (newInput []uint16, newEnd bool) {
			for i := 0; i < len(outputPart); i += 2 {
				if len(inputPart) > 0 && end == false {
					// There are characters left in the filename
					var ucs2Char uint16
					ucs2Char, inputPart = inputPart[0], inputPart[1:]
					bits.PutLE16(outputPart[i:i+2], ucs2Char)
				} else if end == false {
					// We JUST finished the filename -- insert the NULL terminator
					bits.PutLE16(outputPart[i:i+2], 0x0000)
					end = true
				} else {
					// The NULL character has already been inserted. Pad with "0xFFFF"
					bits.PutLE16(outputPart[i:i+2], 0xFFFF)
				}
			}
			return inputPart, end
		}

		nameUCS2Part, end = convFilenamePart(nameUCS2Part, longDirentries[dirIndex].name1[:], end)
		nameUCS2Part, end = convFilenamePart(nameUCS2Part, longDirentries[dirIndex].name2[:], end)
		nameUCS2Part, end = convFilenamePart(nameUCS2Part, longDirentries[dirIndex].name3[:], end)
	}
	longDirentries[0].count |= longLastEntry
	return longDirentries, nil
}

// getShortEntryFromWin takes a long filename (via a callback) and accesses the short filename at the
// end of the LFN components. Also return the number of slots necessary to represent the dirent on
// disk.
func getShortEntryFromWin(callback GetDirentryCallback, startIndex uint) (shortEntry []byte, numDirentrySlots uint8, err error) {
	buf, err := callback(startIndex)
	if err != nil {
		return nil, 0, err
	}
	dl := makeLong(buf)

	if dl.count&longLastEntry == 0 {
		return nil, 0, errLongDirentry
	}
	order := dl.count & longOrdinalMask
	if order == 0 || maxLongDirentries < order {
		return nil, 0, errLongDirentry
	}

	short, err := callback(startIndex + uint(order))
	if err != nil {
		return nil, 0, err
	}

	return short, order + 1, nil
}

// convertWinToUnix reads long direntries, verifies them, and compiles them into a unix name.
func convertWinToUnix(callback GetDirentryCallback, startIndex uint, chksum, highestOrder uint8) (nameUnix string, err error) {
	order := uint8(0)
	nameBuffer := make([]uint16, 0, longnameMaxLen)

	// Iterate in reverse, so we can append to the namebuffer.
	// The "highestOrder-1"-th entry should have an order of "1".
	for direntryIndex := highestOrder - 1; direntryIndex >= 0; direntryIndex-- {
		// Get the next long direntry component
		buf, err := callback(startIndex + uint(direntryIndex))
		if err != nil {
			return "", err
		}
		dl := makeLong(buf)

		// Validate order, checksum
		order = dl.count & longOrdinalMask
		if order != (highestOrder - direntryIndex) { // We must be reading in an increasing order
			return "", errLongDirentry
		} else if order > maxLongDirentries || order == 0 { // The order must be in bounds
			return "", errLongDirentry
		} else if order == highestOrder && (dl.count&longLastEntry == 0) { // The order MUST terminate at highest order
			return "", errLongDirentry
		} else if order != highestOrder && (dl.count&longLastEntry != 0) { // The order shouldn't terminate before that
			return "", errLongDirentry
		} else if chksum != dl.chksum {
			return "", errChecksum
		}

		// Adds a component of the longDirentry to a name buffer.
		// Returns "true" if a null terminator was seen, "false" otherwise.
		convFilenamePart := func(part []uint8) (bool, error) {
			for i := 0; i < len(part); i += 2 {
				charWin := bits.GetLE16(part[i : i+2])
				switch charWin {
				case 0:
					// If the name is null terminated, stop writing to the namebuffer.
					if order != highestOrder {
						// Saw NULL terminator before reaching last order direntry
						return false, errLongDirentry
					}
					return true, nil
				case '/':
					// Forward slashes are not allowed in unix filenames
					return false, errInvalidFilename
				default:
					nameBuffer = append(nameBuffer, charWin)
				}
			}
			return false, nil
		}

		if terminated, err := convFilenamePart(dl.name1[:]); err != nil {
			return "", err
		} else if terminated {
			break
		}
		if terminated, err := convFilenamePart(dl.name2[:]); err != nil {
			return "", err
		} else if terminated {
			break
		}
		if terminated, err := convFilenamePart(dl.name3[:]); err != nil {
			return "", err
		} else if terminated {
			break
		}
	}

	if order != highestOrder {
		panic("Invalid internal order state while iterating; terminated before viewing all direntries")
	}

	result := string(utf16.Decode(nameBuffer))
	if longnameMaxLen < len(result) {
		return "", errTooLong
	}
	return result, nil
}

// Compute the unrolled checksum of a DOS filename for Win95 LFN use.
func checksum(name []uint8) uint8 {
	if len(name) != dosNameLen {
		panic("Checksumming invalid short name")
	}
	var sum uint8
	for i := 0; i < dosNameLen; i++ {
		sum = ((sum & 1) << 7) + (sum >> 1) + name[i]
	}
	return sum
}

func charUnixToDOS(r rune) uint8 {
	// ASCII Rune --> ASCII DOS character. Simple case.
	if r < maxASCII {
		return asciiFilter[r]
	}
	// Non-ASCII Unicode Rune --> Code page 850 character OR "replacement character"
	if dosChar, ok := unicodeToDos[r]; ok {
		return dosChar
	}
	return cRpl
}

func charDOSToUnix(b uint8) rune {
	// ASCII DOS character to ASCII rune. Simple case.
	if b < maxASCII {
		r := asciiFilter[b]
		switch r {
		case cBad, cRpl, cSkp:
			return rune('?')
		default:
			return rune(b)
		}
	}
	return dosToUnicode[b-128]
}
