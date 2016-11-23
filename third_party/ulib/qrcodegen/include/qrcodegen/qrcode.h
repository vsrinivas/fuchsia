/*
 * QR Code generator library (C++)
 *
 * Copyright (c) 2016 Project Nayuki
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * (MIT License)
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#pragma once

#include <stdint.h>

#ifndef __Fuchsia__
#ifndef _KERNEL
#include <vector>
#include <qrcodegen/bitbuffer.h>
#include <qrcodegen/qrsegment.h>
#endif
#endif

namespace qrcodegen {

enum Error : unsigned int {
    None = 0,
    Internal = 1,
    InvalidArgs = 2,
    OutOfSpace = 3,
    BadData = 4,
};

/* Represents the error correction level used in a QR Code symbol. */
enum Ecc : unsigned int {
    LOW = 0,
    MEDIUM = 1,
    QUARTILE = 2,
    HIGH = 3,
};

/*
 * Computes the Reed-Solomon error correction codewords for a sequence of data codewords
 * at a given degree. Objects are immutable, and the state only depends on the degree.
 * This class exists because the divisor polynomial does not need to be recalculated for every input.
 */
class ReedSolomonGenerator final {
private:
    static constexpr size_t kMaxDegree = 255;

    // Coefficients of the divisor polynomial, stored from highest to lowest power, excluding the leading term which
    // is always 1. For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
    uint8_t coefficients_[kMaxDegree];
    size_t degree_;

public:
    ReedSolomonGenerator() : degree_(0) {}

    /*
     * Initialize a Reed-Solomon ECC generator for the given degree. This could be implemented
     * as a lookup table over all possible parameter values, instead of as an algorithm.
     */
    Error init(size_t degree);

    /*
     * Computes and returns the Reed-Solomon error correction codewords for the given sequence of data codewords.
     * The returned object is always a new byte array. This method does not alter this object's state (because it is immutable).
     */
    Error getRemainder(const uint8_t* data, size_t len, uint8_t* result) const;

private:
    // Returns the product of the two given field elements modulo GF(2^8/0x11D). The arguments and result
    // are unsigned 8-bit integers. This could be implemented as a lookup table of 256*256 entries of uint8.
    static Error multiply(uint8_t x, uint8_t y, uint8_t& out);
};

/*
 * Helper to iterate over the bits in an array of data and ecc blocks
 * The data consists of a set of blocks of data and ecc data.
 * The short blocks have a dummy byte after the data and before the ecc
 * data that must be skipped.
 * The bitstream is built from block1 byte1, block2 byte1, ... blockN byte1,
 * block1 byte2, block2 byte2, ... blockN byte2, skipping the dummy byte
 * on short blocks, until all bits have been streamed out.
 */

class Codebits {
public:
    Codebits(uint8_t* data, size_t blocks, size_t blocklen,
             size_t shortblocks, size_t skipbyte) :
        data_(data), i_(0), j_(0), imax_(blocklen), jmax_(blocks),
        shortblocks_(shortblocks), skip_(skipbyte), mask_(0), bits_(0) {
    }
    Codebits() {}

    size_t maxbits() const {
        return (jmax_ * imax_ - shortblocks_) * 8;
    }
    size_t size() const {
        return (jmax_ * imax_ - shortblocks_);
    }
    bool next() {
        while (mask_ == 0) {
            if (i_ < imax_) {
                if ((i_ != skip_) || (j_ >= shortblocks_)) {
                    mask_ = 0x80;
                    bits_ = data_[j_ * imax_ + i_];
                }
                if (++j_ == jmax_) {
                    j_ = 0;
                    ++i_;
                }
            } else {
                return false;
            }
        }
        bool res = ((bits_ & mask_) != 0);
        mask_ >>= 1;
        return res;
    }

private:
    uint8_t* data_;
    size_t i_, j_;
    size_t imax_, jmax_, shortblocks_, skip_;
    unsigned mask_, bits_;
};


/*
 * Represents an immutable square grid of black and white cells for a QR Code symbol, and
 * provides static functions to create a QR Code from user-supplied textual or binary data.
 * This class covers the QR Code model 2 specification, supporting all versions (sizes)
 * from 1 to 40, all 4 error correction levels, and only 3 character encoding modes.
 */
class QrCode final {
public:

    QrCode();

    /*
     * Render a QR Code symbol with the given version number, error correction level, binary data array,
     * and mask number. This is a cumbersome low-level constructor that should not be invoked directly by the user.
     * To go one level up, see the encodeSegments() function.
     */
    Error draw(int ver, Ecc ecl, const uint8_t* data, size_t len, int mask);

    /* Change the mask pattern of the QrCode */
    Error changeMask(int mask);

    /* This QR Code symbol's version number, which is always between 1 and 40 (inclusive). */
    int version() const { return version_; }

    /* The width and height of this QR Code symbol, measured in modules.
     * Always equal to version &times; 4 + 17, in the range 21 to 177. */
    int size() const { return size_; }

    /* The error correction level used in this QR Code symbol. */
    int ecc() const { return ecc_; }

    /* The mask pattern used in this QR Code symbol, in the range 0 to 7 (i.e. unsigned 3-bit integer).
     * Note that even if a constructor was called with automatic masking requested
     * (mask = -1), the resulting object will still have a mask value between 0 and 7. */
    int mask() const { return mask_; }

    /*
     * Returns the color of the module (pixel) at the given coordinates, which is either 0 for white or 1 for black. The top
     * left corner has the coordinates (x=0, y=0). If the given coordinates are out of bounds, then 0 (white) is returned.
     */
    int pixel(int x, int y) const {
        if (0 <= x && x < size_ && 0 <= y && y < size_)
            return (module_[y * kStride + (x >> 3)] & (1 << (x & 7))) != 0;
        else
            return 0;  // Infinite white border
    };

#ifndef __Fuchsia__
#ifndef _KERNEL
    /*
     * Encode a QR Code symbol representing the given Unicode text string at the given error correction level.
     * As a conservative upper bound, this function is guaranteed to succeed for strings that have 738 or fewer Unicode
     * code points (not UTF-16 code units). The smallest possible QR Code version is automatically chosen for the output.
     * The ECC level of the result may be higher than the ecl argument if it can be done without increasing the version.
     */
    Error encodeText(const char *text, Ecc ecl);

    /*
     * Encode a QR Code symbol representing the given binary data string at the given error correction level.
     * This function always encodes using the binary segment mode, not any text mode. The maximum number of
     * bytes allowed is 2953. The smallest possible QR Code version is automatically chosen for the output.
     * The ECC level of the result may be higher than the ecl argument if it can be done without increasing the version.
     */
    Error encodeBinary(const std::vector<uint8_t> &data, Ecc ecl);

    /*
     * Encode a QR Code symbol representing the specified data segments with the specified encoding parameters.
     * The smallest possible QR Code version within the specified range is automatically chosen for the output.
     * This function allows the user to create a custom sequence of segments that switches
     * between modes (such as alphanumeric and binary) to encode text more efficiently.
     * This function is considered to be lower level than simply encoding text or binary data.
     */
    Error encodeSegments(const std::vector<QrSegment> &segs,
                         Ecc ecl, int minVersion=1, int maxVersion=40,
                         int mask=-1, bool boostEcl=true);
#endif
#endif

    Error encodeBinary(const void* data, size_t datalen,
                       Ecc ecl=Ecc::LOW, int minVersion=1, int maxVersion=40,
                       int mask=-1);


private:
    int version_;
    int size_;
    int mask_;
    Ecc ecc_;

    static constexpr size_t kMaxWidth = 177;
    static constexpr size_t kMaxHeight = 177;
    static constexpr size_t kStride = (kMaxWidth + 7) / 8;

    static constexpr size_t kMaxCodeWords = 3706;
    static constexpr size_t kMaxDataWords = 2956;
    static constexpr size_t kMaxBinaryData = 2953;

    // The modules of this QR Code symbol (false = white, true = black)
    uint8_t module_[kStride * kMaxHeight];

    // Indicates function modules that are not subjected to masking
    uint8_t isfunc_[kStride * kMaxHeight];

    // Assembly buffer
    uint8_t codewords_[kMaxCodeWords];
    Codebits codebits_;

    ReedSolomonGenerator rsg_;


    /*
     * Internal accessors.  x,y must be within range.
     */
    bool getModule(int x, int y) const {
        return (module_[y * kStride + (x >> 3)] & (1 << (x & 7))) != 0;
    };

    bool isFunction(int x, int y) const {
        return (isfunc_[y * kStride + (x >> 3)] & (1 << (x & 7))) != 0;
    }

    void setModule(int x, int y, bool yes) {
        if (yes) {
            module_[y * kStride + (x >> 3)] |= static_cast<uint8_t>(1 << (x & 7));
        } else {
            module_[y * kStride + (x >> 3)] &= static_cast<uint8_t>(~(1 << (x & 7)));
        }
    }

    void setFunction(int x, int y) {
        isfunc_[y * kStride + (x >> 3)] |= static_cast<uint8_t>(1 << (x & 7));
    }

    Error drawFunctionPatterns();

    // Draws two copies of the format bits (with its own error correction code)
    // based on the given mask and this object's error correction level field.
    Error drawFormatBits(int mask);

    // Draws two copies of the version bits (with its own error correction code),
    // based on this object's version field (which only has an effect for 7 <= version <= 40).
    Error drawVersion();

    // Draws a 9*9 finder pattern including the border separator, with the center module at (x, y).
    void drawFinderPattern(int x, int y);

    // Draws a 5*5 alignment pattern, with the center module at (x, y).
    void drawAlignmentPattern(int x, int y);

    // Sets the color of a module and marks it as a function module.
    // Only used by the constructor. Coordinates must be in range.
    void setFunctionModule(int x, int y, bool isBlack);


    /*---- Private helper methods for constructor: Codewords and masking ----*/
private:

    // Computes the error correction codewords and then calls drawCodewords()
    // codebits_ is ready for use on success
    Error computeCodewords(const uint8_t* data, size_t len);

    // Draws the given sequence of 8-bit codewords (data and error correction) onto the entire
    // data area of this QR Code symbol. Function modules need to be marked off before this is called.
    // Codewords are provided by codebits_
    Error drawCodewords();

    // XORs the data modules in this QR Code with the given mask pattern. Due to XOR's mathematical
    // properties, calling applyMask(m) twice with the same value is equivalent to no change at all.
    // This means it is possible to apply a mask, undo it, and try another mask. Note that a final
    // well-formed QR Code symbol needs exactly one mask applied (not zero, not two, etc.).
    Error applyMask(int mask);

    // A messy helper function for the constructors. This QR Code must be in an unmasked state when this
    // method is called. The given argument is the requested mask, which is -1 for auto or 0 to 7 for fixed.
    // This method applies and returns the actual mask chosen, from 0 to 7.
    Error handleConstructorMasking(int mask);

    // Calculates and returns the penalty score based on state of this QR Code's current modules.
    // This is used by the automatic mask choice algorithm to find the mask pattern that yields the lowest score.
    int getPenaltyScore() const;


    // Returns a set of positions of the alignment patterns in ascending order. These positions are
    // used on both the x and y axes. Each value in the resulting array is in the range [0, 177).
    // This stateless pure function could be implemented as table of 40 variable-length lists of unsigned bytes.
    static constexpr size_t kMaxAlignMarks = 7;
    static int getAlignmentPatternPositions(int ver, int out[kMaxAlignMarks]);

    // Returns the number of raw data modules (bits) available at the given version number.
    // These data modules are used for both user data codewords and error correction codewords.
    // This stateless pure function could be implemented as a 40-entry lookup table.
    static int getNumRawDataModules(int ver);

    // Returns the number of 8-bit data (i.e. not error correction) codewords contained in any
    // QR Code of the given version number and error correction level, with remainder bits discarded.
    // This stateless pure function could be implemented as a (40*4)-cell lookup table.
    static int getNumDataCodewords(int ver, const Ecc &ecl);


    // For use in getPenaltyScore(), when evaluating which mask is best.
    static const int PENALTY_N1;
    static const int PENALTY_N2;
    static const int PENALTY_N3;
    static const int PENALTY_N4;

    static const int16_t NUM_ERROR_CORRECTION_CODEWORDS[4][41];
    static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41];
};

}
