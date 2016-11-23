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

#include <assert.h>
#include <string.h>

#include <qrcodegen/qrcode.h>

namespace qrcodegen {

class BitBufferFiller {
public:
    BitBufferFiller(uint8_t* buffer, size_t len) :
        data_(buffer), maxbits_(len * 8), bitlen_(0), valid_(true) {
        memset(buffer, 0, len);
    }

    size_t bitlen() { return bitlen_; }
    bool valid() { return valid_; }

    void appendBits(uint32_t val, size_t len) {
        if ((maxbits_ - bitlen_) < len) {
            valid_ = false;
            return;
        }

        for (int i = (int)len - 1; i >= 0; i--) {
            data_[bitlen_ >> 3] |= static_cast<uint8_t>(((val >> i) & 1) << (7 - (bitlen_ & 7)));
            ++bitlen_;
        }
    }

    void appendData(const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        while (len > 0) {
            appendBits(*bytes++, 8);
            len--;
        }
    }

private:
    uint8_t* data_;
    size_t maxbits_;
    size_t bitlen_;
    bool valid_;
};

int eccOrdinal(Ecc ecc) {
    if (ecc > Ecc::HIGH)
        return 0;
    return ecc;
}

int eccFormatBits(Ecc ecc) {
    switch (ecc) {
    case Ecc::LOW: return 1;
    case Ecc::MEDIUM: return 0;
    case Ecc::QUARTILE: return 3;
    case Ecc::HIGH: return 2;
    default: return 1;
    }
}

#ifndef __Fuchsia__
#ifndef _KERNEL

Error QrCode::encodeText(const char *text, Ecc ecl) {
    std::vector<QrSegment> segs(QrSegment::makeSegments(text));
    return encodeSegments(segs, ecl);
}


Error QrCode::encodeBinary(const std::vector<uint8_t> &data, Ecc ecl) {
    std::vector<QrSegment> segs;
    segs.push_back(QrSegment::makeBytes(data));
    return encodeSegments(segs, ecl);
}


Error QrCode::encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
        int minVersion, int maxVersion, int mask, bool boostEcl) {
    if (!(1 <= minVersion && minVersion <= maxVersion && maxVersion <= 40) || mask < -1 || mask > 7)
        return Error::InvalidArgs;

    // Find the minimal version number to use
    int version, dataUsedBits;
    for (version = minVersion; ; version++) {
        int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;  // Number of data bits available
        dataUsedBits = QrSegment::getTotalBits(segs, version);
        if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
            break;  // This version number is found to be suitable
        if (version >= maxVersion)  // All versions in the range could not fit the given data
            return Error::OutOfSpace;
    }
    if (dataUsedBits == -1)
        return Error::Internal;

    // Increase the error correction level while the data still fits in the current version number
    Ecc newEcl = ecl;
    if (boostEcl) {
        if (dataUsedBits <= getNumDataCodewords(version, Ecc::MEDIUM  ) * 8)
            newEcl = Ecc::MEDIUM;
        if (dataUsedBits <= getNumDataCodewords(version, Ecc::QUARTILE) * 8)
            newEcl = Ecc::QUARTILE;
        if (dataUsedBits <= getNumDataCodewords(version, Ecc::HIGH    ) * 8)
            newEcl = Ecc::HIGH;
    }

    // Create the data bit string by concatenating all segments
    int dataCapacityBits = getNumDataCodewords(version, newEcl) * 8;
    BitBuffer bb;
    for (size_t i = 0; i < segs.size(); i++) {
        const QrSegment &seg(segs.at(i));
        bb.appendBits(seg.mode.modeBits, 4);
        bb.appendBits(seg.numChars, seg.mode.numCharCountBits(version));
        bb.appendData(seg);
    }

    // Add terminator and pad up to a byte if applicable
    bb.appendBits(0, std::min(4, dataCapacityBits - bb.getBitLength()));
    bb.appendBits(0, (8 - bb.getBitLength() % 8) % 8);

    // Pad with alternate bytes until data capacity is reached
    for (uint8_t padByte = 0xEC; bb.getBitLength() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
        bb.appendBits(padByte, 8);

    if (!bb.isValid())
        return Error::BadData;
    if (bb.getBitLength() % 8 != 0)
        return Error::Internal;

    // Create the QR Code symbol
    return draw(version, newEcl, bb.getBytes().data(), bb.getBytes().size(), mask);
}
#endif
#endif

Error QrCode::encodeBinary(const void* data, size_t datalen, Ecc ecl,
        int minVersion, int maxVersion, int mask) {
    if (!(1 <= minVersion && minVersion <= maxVersion && maxVersion <= 40) || mask < -1 || mask > 7)
        return Error::InvalidArgs;

    // Find the minimal version number to use
    int version;
    size_t sizeBits;
    size_t dataUsedBits;
    size_t dataCapacityBits;
    for (version = minVersion; version <= maxVersion; version++) {
        sizeBits = (version < 10) ? 8 : 16;
        dataUsedBits = 4 + sizeBits + datalen * 8;
        dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
        if (dataUsedBits <= dataCapacityBits)
            goto match;
    }
    return Error::OutOfSpace;

match:
    // we use the module_ array (which will be erased and
    // redrawn in draw() as temporary storage here).
    static_assert(sizeof(module_) >= kMaxDataWords, "");

    BitBufferFiller bb(module_, kMaxDataWords);

    // Header: Mode(4bits) = BYTE(4), Count(16bits) = datalen
    bb.appendBits(4, 4);
    bb.appendBits(static_cast<uint32_t>(datalen), sizeBits);
    bb.appendData(data, datalen);

    // Add terminator and pad up to a byte if applicable
    size_t leftover = dataCapacityBits - bb.bitlen();
    bb.appendBits(0, (leftover > 4) ? 4 : leftover);
    bb.appendBits(0, (8 - bb.bitlen() % 8) % 8);

    // Pad with alternate bytes until data capacity is reached
    for (uint8_t padByte = 0xEC; bb.bitlen() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
        bb.appendBits(padByte, 8);

    if (!bb.valid())
        return Error::BadData;
    if (bb.bitlen() % 8 != 0)
        return Error::Internal;

    // Create the QR Code symbol
    return draw(version, ecl, module_, bb.bitlen() / 8, mask);
}

QrCode::QrCode() : version_(1), size_(21), ecc_(Ecc::LOW) {
}

Error QrCode::draw(int ver, Ecc ecl, const uint8_t* data, size_t len, int mask) {

    // Check arguments
    if (ver < 1 || ver > 40 || mask < -1 || mask > 7 || ecl > 3)
        return Error::InvalidArgs;

    // Initialize scalar fields
    version_ = ver;
    size_ = (1 <= ver && ver <= 40 ? ver * 4 + 17 : -1),  // Avoid signed overflow undefined behavior
    ecc_ = ecl;

    Error e;
    if ((e = computeCodewords(data, len)))
        return e;

    // only clear these *after* the computation
    // as they may be used as input buffers
    memset(module_, 0, sizeof(module_));
    memset(isfunc_, 0, sizeof(isfunc_));

    // Draw function patterns, draw all codewords, do masking
    if ((e = drawFunctionPatterns())) {
        return e;
    }
    if ((e = drawCodewords())) {
        return e;
    }
    if ((e = handleConstructorMasking(mask))) {
        return e;
    }
    return Error::None;
}



Error QrCode::changeMask(int mask) {
    // Check arguments
    if (mask < -1 || mask > 7)
        return Error::InvalidArgs;

    // Handle masking
    applyMask(mask_);  // Undo old mask
    handleConstructorMasking(mask);

    return Error::None;
}


Error QrCode::drawFunctionPatterns() {
    // Draw the horizontal and vertical timing patterns
    for (int i = 0; i < size_; i++) {
        setFunctionModule(6, i, i % 2 == 0);
        setFunctionModule(i, 6, i % 2 == 0);
    }

    // Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
    drawFinderPattern(3, 3);
    drawFinderPattern(size_ - 4, 3);
    drawFinderPattern(3, size_ - 4);

    // Draw the numerous alignment patterns
    int offsets[kMaxAlignMarks];
    int numAlign = getAlignmentPatternPositions(version_, offsets);
    for (int i = 0; i < numAlign; i++) {
        for (int j = 0; j < numAlign; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0))
                continue;  // Skip the three finder corners
            else
                drawAlignmentPattern(offsets[i], offsets[j]);
        }
    }

    Error e;

    // Draw configuration data
    // Dummy mask value; overwritten later in the constructor
    if ((e = drawFormatBits(0)))
        return e;

    return drawVersion();
}


Error QrCode::drawFormatBits(int mask) {
    // Calculate error correction code and pack bits
    int data = eccFormatBits(ecc_) << 3 | mask;  // errCorrLvl is uint2, mask is uint3
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    data = data << 10 | rem;
    data ^= 0x5412;  // uint15
    if (data >> 15 != 0)
        return Error::Internal;

    // Draw first copy
    for (int i = 0; i <= 5; i++)
        setFunctionModule(8, i, ((data >> i) & 1) != 0);
    setFunctionModule(8, 7, ((data >> 6) & 1) != 0);
    setFunctionModule(8, 8, ((data >> 7) & 1) != 0);
    setFunctionModule(7, 8, ((data >> 8) & 1) != 0);
    for (int i = 9; i < 15; i++)
        setFunctionModule(14 - i, 8, ((data >> i) & 1) != 0);

    // Draw second copy
    for (int i = 0; i <= 7; i++)
        setFunctionModule(size_ - 1 - i, 8, ((data >> i) & 1) != 0);
    for (int i = 8; i < 15; i++)
        setFunctionModule(8, size_ - 15 + i, ((data >> i) & 1) != 0);
    setFunctionModule(8, size_ - 8, true);

    return Error::None;
}


Error QrCode::drawVersion() {
    if (version_ < 7)
        return Error::None;

    // Calculate error correction code and pack bits
    int rem = version_;  // version is uint6, in the range [7, 40]
    for (int i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    int data = version_ << 12 | rem;  // uint18
    if (data >> 18 != 0)
        return Error::Internal;

    // Draw two copies
    for (int i = 0; i < 18; i++) {
        bool bit = ((data >> i) & 1) != 0;
        int a = size_ - 11 + i % 3, b = i / 3;
        setFunctionModule(a, b, bit);
        setFunctionModule(b, a, bit);
    }

    return Error::None;
}

static int max(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

static int abs(int n) {
    if (n < 0) {
        return -n;
    } else {
        return n;
    }
}

void QrCode::drawFinderPattern(int x, int y) {
    for (int i = -4; i <= 4; i++) {
        for (int j = -4; j <= 4; j++) {
            int dist = max(abs(i), abs(j));  // Chebyshev/infinity norm
            int xx = x + j, yy = y + i;
            if (0 <= xx && xx < size_ && 0 <= yy && yy < size_)
                setFunctionModule(xx, yy, dist != 2 && dist != 4);
        }
    }
}


void QrCode::drawAlignmentPattern(int x, int y) {
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++)
            setFunctionModule(x + j, y + i, max(abs(i), abs(j)) != 1);
    }
}


void QrCode::setFunctionModule(int x, int y, bool isBlack) {
    setModule(x, y, isBlack);
    setFunction(x, y);
}


Error QrCode::computeCodewords(const uint8_t* data, size_t len) {
    if (len != static_cast<unsigned int>(getNumDataCodewords(version_, ecc_)))
        return Error::InvalidArgs;

    // Calculate parameter numbers
    int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[ecc_][version_];
    int totalEcc = NUM_ERROR_CORRECTION_CODEWORDS[ecc_][version_];
    if (totalEcc % numBlocks != 0)
        return Error::Internal;

    int blockEccLen = totalEcc / numBlocks;
    int numShortBlocks = numBlocks - getNumRawDataModules(version_) / 8 % numBlocks;
    int shortBlockLen = getNumRawDataModules(version_) / 8 / numBlocks;
    int fullBlockLen = shortBlockLen + 1;

    // Split data into blocks and append ECC to each block
    Error e;
    if ((e = rsg_.init(blockEccLen)))
        return e;

    if (static_cast<size_t>(fullBlockLen * numBlocks) > sizeof(codewords_))
        return Error::Internal;

    uint8_t* outptr = codewords_;

    for (int i = 0, k = 0; i < numBlocks; i++) {
        int blocklen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);

        memcpy(outptr, data + k, blocklen);
        outptr += blocklen;

        if (i < numShortBlocks)
            *outptr++ = 0;

        rsg_.getRemainder(data + k, blocklen, outptr);
        outptr += blockEccLen;

        k += blocklen;
    }

    Codebits codebits(codewords_, numBlocks, fullBlockLen, numShortBlocks, shortBlockLen - blockEccLen);
    codebits_ = codebits;

    return Error::None;
}


Error QrCode::drawCodewords() {
    if (codebits_.size() != static_cast<unsigned int>(getNumRawDataModules(version_) / 8))
        return Error::InvalidArgs;

    size_t count = codebits_.maxbits();

    // Do the funny zigzag scan
    for (int right = size_ - 1; right >= 1; right -= 2) {  // Index of right column in each column pair
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size_; vert++) {  // Vertical counter
            for (int j = 0; j < 2; j++) {
                int x = right - j;  // Actual x coordinate
                bool upwards = ((right & 2) == 0) ^ (x < 6);
                int y = upwards ? size_ - 1 - vert : vert;  // Actual y coordinate
                if (!isFunction(x,y) && (count > 0)) {
                    setModule(x, y, codebits_.next());
                    count--;
                }
                // If there are any remainder bits (0 to 7), they are already
                // set to 0/false/white when the grid of modules was initialized
            }
        }
    }
    if (count != 0)
        return Error::Internal;

    return Error::None;
}


Error QrCode::applyMask(int mask) {
    if (mask < 0 || mask > 7)
        return Error::InvalidArgs;
    for (int y = 0; y < size_; y++) {
        for (int x = 0; x < size_; x++) {
            bool invert;
            switch (mask) {
                case 0:  invert = (x + y) % 2 == 0;                    break;
                case 1:  invert = y % 2 == 0;                          break;
                case 2:  invert = x % 3 == 0;                          break;
                case 3:  invert = (x + y) % 3 == 0;                    break;
                case 4:  invert = (x / 3 + y / 2) % 2 == 0;            break;
                case 5:  invert = x * y % 2 + x * y % 3 == 0;          break;
                case 6:  invert = (x * y % 2 + x * y % 3) % 2 == 0;    break;
                case 7:  invert = ((x + y) % 2 + x * y % 3) % 2 == 0;  break;
                default:  return Error::Internal;
            }
            if (!isFunction(x, y) && invert) {
                setModule(x, y, !getModule(x, y));
            }
        }
    }
    return Error::None;
}


Error QrCode::handleConstructorMasking(int mask) {
    if (mask == -1) {  // Automatically choose best mask
        int32_t minPenalty = INT32_MAX;
        for (int i = 0; i < 8; i++) {
            Error e;
            if ((e = drawFormatBits(i)))
                return e;
            if ((e = applyMask(i)))
                return e;
            int penalty = getPenaltyScore();
            if (penalty < minPenalty) {
                mask = i;
                minPenalty = penalty;
            }
            // Undoes the mask due to XOR
            if ((e = applyMask(i)))
                return e;
        }
    }
    if (mask < 0 || mask > 7)
        return Error::Internal;

    Error e;
    // Overwrite old format bits
    if ((e = drawFormatBits(mask)))
        return e;
    // Apply the final choice of mask
    if ((e = applyMask(mask)))
        return e;

    mask_ = mask;
    return Error::None;
}


int QrCode::getPenaltyScore() const {
    int result = 0;

    // Adjacent modules in row having same color
    for (int y = 0; y < size_; y++) {
        bool colorX = getModule(0, y);
        for (int x = 1, runX = 1; x < size_; x++) {
            if (getModule(x, y) != colorX) {
                colorX = getModule(x, y);
                runX = 1;
            } else {
                runX++;
                if (runX == 5)
                    result += PENALTY_N1;
                else if (runX > 5)
                    result++;
            }
        }
    }
    // Adjacent modules in column having same color
    for (int x = 0; x < size_; x++) {
        bool colorY = getModule(x, 0);
        for (int y = 1, runY = 1; y < size_; y++) {
            if (getModule(x, y) != colorY) {
                colorY = getModule(x, y);
                runY = 1;
            } else {
                runY++;
                if (runY == 5)
                    result += PENALTY_N1;
                else if (runY > 5)
                    result++;
            }
        }
    }

    // 2*2 blocks of modules having same color
    for (int y = 0; y < size_ - 1; y++) {
        for (int x = 0; x < size_ - 1; x++) {
            bool  color = getModule(x, y);
            if (  color == getModule(x + 1, y) &&
                  color == getModule(x, y + 1) &&
                  color == getModule(x + 1, y + 1))
                result += PENALTY_N2;
        }
    }

    // Finder-like pattern in rows
    for (int y = 0; y < size_; y++) {
        for (int x = 0, bits = 0; x < size_; x++) {
            bits = ((bits << 1) & 0x7FF) | (getModule(x, y) ? 1 : 0);
            if (x >= 10 && (bits == 0x05D || bits == 0x5D0))  // Needs 11 bits accumulated
                result += PENALTY_N3;
        }
    }
    // Finder-like pattern in columns
    for (int x = 0; x < size_; x++) {
        for (int y = 0, bits = 0; y < size_; y++) {
            bits = ((bits << 1) & 0x7FF) | (getModule(x, y) ? 1 : 0);
            if (y >= 10 && (bits == 0x05D || bits == 0x5D0))  // Needs 11 bits accumulated
                result += PENALTY_N3;
        }
    }

    // Balance of black and white modules
    int black = 0;
    for (int y = 0; y < size_; y++) {
        for (int x = 0; x < size_; x++) {
            if (getModule(x, y))
                black++;
        }
    }
    int total = size_ * size_;
    // Find smallest k such that (45-5k)% <= dark/total <= (55+5k)%
    for (int k = 0; black*20 < (9-k)*total || black*20 > (11+k)*total; k++)
        result += PENALTY_N4;
    return result;
}


int QrCode::getAlignmentPatternPositions(int ver, int out[kMaxAlignMarks]) {
    if (ver == 1) {
        return 0;
    } else {
        int numAlign = ver / 7 + 2;
        int step;
        if (ver != 32)
            step = (ver * 4 + numAlign * 2 + 1) / (2 * numAlign - 2) * 2;  // ceil((size - 13) / (2*numAlign - 2)) * 2
        else  // C-C-C-Combo breaker!
            step = 26;

        int size = ver * 4 + 17;
        int j = numAlign - 1;
        for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
            out[j--] = pos;
        out[0] = 6;
        return numAlign;
    }
}


int QrCode::getNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 18 * 2;  // Subtract version information
    }
    return result;
}


int QrCode::getNumDataCodewords(int ver, const Ecc &ecl) {
    return getNumRawDataModules(ver) / 8 - NUM_ERROR_CORRECTION_CODEWORDS[ecl][ver];
}


/*---- Tables of constants ----*/

const int QrCode::PENALTY_N1 = 3;
const int QrCode::PENALTY_N2 = 3;
const int QrCode::PENALTY_N3 = 40;
const int QrCode::PENALTY_N4 = 10;


const int16_t QrCode::NUM_ERROR_CORRECTION_CODEWORDS[4][41] = {
    // Version: (note that index 0 is for padding, and is set to an illegal value)
    //0,  1,  2,  3,  4,  5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,   25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35,   36,   37,   38,   39,   40    Error correction level
    {-1,  7, 10, 15, 20, 26,  36,  40,  48,  60,  72,  80,  96, 104, 120, 132, 144, 168, 180, 196, 224, 224, 252, 270, 300,  312,  336,  360,  390,  420,  450,  480,  510,  540,  570,  570,  600,  630,  660,  720,  750},  // Low
    {-1, 10, 16, 26, 36, 48,  64,  72,  88, 110, 130, 150, 176, 198, 216, 240, 280, 308, 338, 364, 416, 442, 476, 504, 560,  588,  644,  700,  728,  784,  812,  868,  924,  980, 1036, 1064, 1120, 1204, 1260, 1316, 1372},  // Medium
    {-1, 13, 22, 36, 52, 72,  96, 108, 132, 160, 192, 224, 260, 288, 320, 360, 408, 448, 504, 546, 600, 644, 690, 750, 810,  870,  952, 1020, 1050, 1140, 1200, 1290, 1350, 1440, 1530, 1590, 1680, 1770, 1860, 1950, 2040},  // Quartile
    {-1, 17, 28, 44, 64, 88, 112, 130, 156, 192, 224, 264, 308, 352, 384, 432, 480, 532, 588, 650, 700, 750, 816, 900, 960, 1050, 1110, 1200, 1260, 1350, 1440, 1530, 1620, 1710, 1800, 1890, 1980, 2100, 2220, 2310, 2430},  // High
};

const int8_t QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    // Version: (note that index 0 is for padding, and is set to an illegal value)
    //0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},  // Low
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},  // Medium
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},  // High
};


Error ReedSolomonGenerator::init(size_t degree) {
    if (degree < 1 || degree > kMaxDegree)
        return Error::InvalidArgs;

    degree_ = degree;

    // Start with the monomial x^0
    memset(coefficients_, 0, degree - 1);
    coefficients_[degree - 1] = 1;

    // Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
    // drop the highest term, and store the rest of the coefficients in order of descending powers.
    // Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
    int root = 1;
    for (size_t i = 0; i < degree; i++) {
        // Multiply the current product by (x - r^i)
        for (size_t j = 0; j < degree; j++) {
            uint8_t n;
            if (multiply(coefficients_[j], static_cast<uint8_t>(root), n))
                return Error::Internal;
            coefficients_[j] = n;

            if (j + 1 < degree)
                coefficients_[j] ^= coefficients_[j + 1];
        }
        root = (root << 1) ^ ((root >> 7) * 0x11D);  // Multiply by 0x02 mod GF(2^8/0x11D)
    }
    return Error::None;
}

Error ReedSolomonGenerator::getRemainder(const uint8_t* data, size_t len, uint8_t* result) const {
    // Compute the remainder by performing polynomial division
    memset(result, 0, degree_);
    for (size_t i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ result[0];
        memmove(result, result + 1, degree_ - 1);
        result[degree_ - 1] = 0;
        for (size_t j = 0; j < degree_; j++) {
            uint8_t n;
            if (multiply(coefficients_[j], factor, n))
                return Error::Internal;
            result[j] ^= n;
        }
    }

    return Error::None;
}


Error ReedSolomonGenerator::multiply(uint8_t x, uint8_t y, uint8_t& out) {
    // Russian peasant multiplication
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    if (z >> 8 != 0)
        return Error::Internal;

    out = static_cast<uint8_t>(z);
    return Error::None;
}

}; // namespace qrcodegen
