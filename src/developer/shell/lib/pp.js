// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Utilities for printing objects nicely.
 */

/**
 * Prints an object on the given fd.
 *
 * @param {*} fd Something like "std.out" or "std.err"
 * @param {*} obj The object to print.
 */
function fprint(fd, obj) {
  // Implementation note: JSON.stringify dies with BigInt and friends, so we do it the hard way.
  if (obj == null) {
    fd.printf('null');
    return;
  }
  if (Array.isArray(obj)) {
    fd.printf('[');
    for (let i = 0; i < obj.length; i++) {
      if (i != 0) {
        fd.printf(',');
      }
      fprint(fd, obj[i]);
    }
    fd.printf(']');
    return;
  }
  let type = (typeof obj);
  switch (type) {
    case 'number':
      fd.printf(Number(obj).toString(10));
      return;
    case 'string':
      fd.printf('"' + obj + '"');
      return;
    case 'boolean':
      fd.printf(obj ? 'true' : 'false');
      return;
    case 'object':
      fd.printf('{');
      let first = true;
      for (const property in obj) {
        if (!first) {
          fd.printf(', ')
        }
        fprint(fd, property);
        fd.printf(':');
        fprint(fd, obj[property]);
        first = false;
      }
      fd.printf('}');
      return;
  }
  if (typeof obj.toString == 'function') {
    fd.printf(obj.toString());
    return;
  }
  throw 'Unknown object type for ' + obj;
}

/**
 * Prints an object on stdout.
 *
 * @param {*} obj The object to print.
 */
function print(obj) {
  fprint(std.out, obj)
}


export {print, fprint}