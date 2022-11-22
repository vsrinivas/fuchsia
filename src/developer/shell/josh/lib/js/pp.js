// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Utilities for printing objects nicely.
 */

/**
 * Returns a string containing a representation of the object
 *
 * @param {*} obj The object to print
 * @param {*} options Optional boolean parameters.
 *     If this contains a property called "whitespace", you will get pretty printing.
 *     If this contains a property called "quotes", you will get quotation
 *        marks around strings.
 * @param {*} depth The function is recursive; this indicates the levels
 *                  of recursive depth.
 */
function sprint_go(obj, options, depth) {
  let whitespace = options.hasOwnProperty('whitespace');
  let quote = options.hasOwnProperty('quotes') ? '"' : '';
  let output = '';
  let newline = '';
  let indent = '';
  let outdent = '';

  let type = (typeof obj);
  if (whitespace) {
    newline = '\n';
    for (let i = 0; i < depth - 1; i++) {
      outdent += ' ';
    }
    indent = outdent + ' ';
  }

  // Implementation note: JSON.stringify dies with BigInt and friends, so we do it the hard way.
  if (obj == null) {
    output += 'null';
    return output;
  }
  if (Array.isArray(obj)) {
    output += '[' + newline;
    for (let i = 0; i < obj.length; i++) {
      if (i != 0) {
        output += ',' + newline;
      }
      output += indent;
      output += sprint_go(obj[i], options, depth + 1);
    }
    output += newline + outdent + ']';
    return output;
  }
  switch (type) {
    case 'number':
      output += Number(obj).toString(10);
      return output;
    case 'string':
      output += quote + obj + quote;
      return output;
    case 'boolean':
      output += obj ? 'true' : 'false';
      return output;
    case 'object':
      output += '{' + newline;
      let first = true;
      for (const property in obj) {
        if (!first) {
          output += ',' + newline;
        }
        output += indent;
        output += sprint_go(property, options, depth + 1);
        output += ' : ';
        output += sprint_go(obj[property], options, depth + 1);
        first = false;
      }
      output += newline + outdent + '}';
      return output;
  }
  if (typeof obj.toString == 'function') {
    output += obj.toString();
    return output;
  }
  throw 'Unknown object type for ' + obj;
}


/**
 * Returns a pretty printed representation of this object.
 *
 * @param {*} obj
 * @param {*} options Optional boolean parameters.
 *     If this contains a property called "whitespace", you will get pretty printing.
 *     If this contains a property called "quotes", you will get quotation
 *        marks around strings.
 */
function sprint(obj, options) {
  if (!options) {
    options = {quotes: true};
  }
  return sprint_go(obj, options, 1);
}

/**
 * Prints an object on the given fd.
 *
 * @param {*} fd Something like "std.out" or "std.err"
 * @param {*} obj The object to print.
 * @param {*} options Optional boolean parameters (see sprint)
 */
function fprint(fd, obj, options) {
  if (!options) {
    options = {quotes: true, whitespace: true};
  }
  fd.printf('%s', sprint_go(obj, options, 1));
}

/**
 * Prints an object on stdout.
 *
 * @param {*} obj The object to print.
 * @param {*} options Optional boolean parameters (see sprint)
 */
function print(obj, options) {
  fprint(std.out, obj, options)
}

/**
 * Returns a string containing a tabular representation of the given array.
 * The given array is an array of objects.  Each property is one column in
 * the table. The first element is assumed to contain exactly the properties
 * to be printed. If subsequent elements are missing some of those properties,
 * an exception will be thrown.  If subsequent elements have additional
 * properties, they will not be printed.
 *
 * @param {*} arr The array to print in tabular form.
 */
function scols(arr) {
  let schema = [];
  let outArr = [];
  let colWidths = [];

  if (!Array.isArray(arr)) {
    throw 'Attempt to print non-array value';
  }

  if (arr.length < 1) {
    throw 'No columns for array detected'
  }

  for (let prop in arr[0]) {
    if (arr[0].hasOwnProperty(prop)) {
      schema.push(prop);
      colWidths.push(prop.length >= 1 ? prop.length + 1 : 1);
    }
  }

  for (let i = 0; i < arr.length; i++) {
    let newElt = [];
    for (let j = 0; j < schema.length; j++) {
      let str = sprint_go(arr[i][schema[j]], {}, 1);
      if (colWidths[j] < str.length + 1) {
        colWidths[j] = str.length + 1;
      }
      newElt.push(str);
    }
    outArr.push(newElt);
  }

  let output = '\n';
  for (let i = 0; i < schema.length; i++) {
    output += schema[i].padStart(colWidths[i], ' ');
  }
  output += '\n';
  for (let i = 0; i < outArr.length; i++) {
    for (let j = 0; j < schema.length; j++) {
      output += outArr[i][j].padStart(colWidths[j], ' ');
    }
    output += '\n';
  }
  return output;
}

/**
 * Prints a tabular representation of the given array arr to file descriptor
 * fd.  Example: pp,fpcols(std.err, []).
 *
 * For more on the tabular representation, see scols.
 *
 * @param {*} fd The file descriptor to which to write
 * @param {*} arr The array to print to that file descriptor.
 */
function fpcols(fd, arr) {
  fd.printf('%s', scols(arr));
}

/**
 * Prints a tabular representation of the given array arr to stdout.
 *
 * For more on the tabular representation, see scols.
 *
 * @param {*} arr The array to print.
 */
function pcols(arr) {
  fpcols(std.out, arr);
}

export {print, fprint, sprint, pcols, fpcols, scols}