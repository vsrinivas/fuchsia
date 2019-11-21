// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview A grab-bag of utilities.
 */

// TextDecoder doesn't come with quickjs.
/**
 * Produces a String given an array of UTF-16 codepoints.
 *
 * @param {Number[]} codePoints An array of codepoints to convert to a String
 */
function codePointsToString(codePoints) {
  let s = '';
  let i = 0;
  const length = codePoints.length;
  const BATCH_SIZE = 10000;
  while (i < length) {
    const end = Math.min(i + BATCH_SIZE, length);
    const slice = codePoints.slice(i, end);
    s += String.fromCodePoint.apply(null, slice);
    i = end;
  }
  return s;
}

/**
 * Produces a String from a DataView into UTF-8 bytes.
 *
 * @param {DataView} bytes A view into an array of UTF-8 bytes to convert to a String.
 */
function decodeUtf8(bytes) {
  let offset = 0;
  const codePoints = [];
  while (offset < bytes.byteLength) {
    const c = bytes.getUint8(offset++);
    if (c < 0x80) {  // Regular 7-bit ASCII.
      codePoints.push(c);
    } else if (c < 0xC0) {
      // UTF-8 continuation mark. We are out of sync. This
      // might happen if we attempted to read a character
      // with more than four bytes.
      continue;
    } else if (c < 0xE0) {  // UTF-8 with two bytes.
      const c2 = bytes.getUint8(offset++);
      codePoints.push(((c & 0x1F) << 6) | (c2 & 0x3F));
    } else if (c < 0xF0) {  // UTF-8 with three bytes.
      const c2 = bytes.getUint8(offset++);
      const c3 = bytes.getUint8(offset++);
      codePoints.push(((c & 0xF) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
    } else if (c < 0xF8) {  // UTF-8 with 4 bytes.
      const c2 = bytes.getUint8(offset++);
      const c3 = bytes.getUint8(offset++);
      const c4 = bytes.getUint8(offset++);
      // Characters written on 4 bytes have 21 bits for a codepoint.
      // We can't fit that on 16bit characters, so we use surrogates.
      let codepoint = ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
      codePoints.push(codepoint);
    }
  }
  return codePointsToString(codePoints);
}

/**
 * Returns whether the two arrays are equal.
 *
 * @param {Any[]} a
 * @param {Any[]} b
 */
function arraysEqual(a, b) {
  if (a === b) {
    return true;
  }
  if (a == null || b == null) {
    return false;
  }
  if (a.length != b.length) {
    return false;
  }
  for (var i = 0; i < a.length; ++i) {
    if (a[i] !== b[i]) {
      return false;
    }
  }
  return true;
}

/**
 * Returns whether a is a prefix of b.
 *
 * @param {Any[]} a
 * @param {Any[]} b
 */
function isPrefixOf(a, b) {
  if (a === b) {
    return true;
  }
  if (a == null || b == null) {
    return false;
  }
  if (a.length > b.length) {
    return false;
  }
  for (let i = 0; i < a.length; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

export {isPrefixOf, arraysEqual, codePointsToString, decodeUtf8}
