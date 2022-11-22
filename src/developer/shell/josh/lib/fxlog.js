// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import * as fxlog_internal from 'fxlog_internal';

(function(global) {

function get_caller_info() {
    let file = "<unknown>";
    let line = 0;
    let func = "<unknown>";

    // Skip current and the lower call functions so we get the real file/line/func
    // that calls logging functions.
    let info = new Error().stack.split('\n')[2].trim().split(' ');

    if (info.length === 3) {
        func = info[1];

        // looking for something like "(/path/to/script.js:10) and we will be cutting
        // "([/path/to/[script.js]]:[10])
        let match = info[2].match(/^\(((.*\/)?(.+))\:([0-9]+)\)$/);
        if (match != null) {
            file = match[3];
            line = match[4];
        } else if (info[2] == "(<evalScript>)") {
            file = "<evalScript>";
            line = 1;
        } else {
            fxlog_internal.warn(`Failed to parse caller file/line from "${info[2]}"`);
        }
    } else {
        fxlog_internal.warn(`Failed to parse caller info "${info}"`);
    }

    // If file/line/func format failed to match above, will return
    // file = "<unknown>", line = 0, func = "<unknown>"

    return [file, line, func]
}

function trace(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    return fxlog_internal.trace(tag, msg, file, line);
}

function debug(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    fxlog_internal.debug(tag, msg, file, line);
}

function info(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    fxlog_internal.info(tag, msg, file, line);
}

function warn(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    fxlog_internal.warn(tag, msg, file, line);
}

function error(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    fxlog_internal.error(tag, msg, file, line);
}

function fatal(msg, tag=null) {
    var [file, line, func] = get_caller_info();
    if (tag == null) tag = func;
    fxlog_internal.fatal(tag, msg, file, line);
}

var trace_raw = fxlog_internal.trace;
var debug_raw = fxlog_internal.debug;
var info_raw = fxlog_internal.info;
var warn_raw = fxlog_internal.warning;
var error_raw = fxlog_internal.error;
var fatal_raw = fxlog_internal.fatal;

global['fxlog'] = {
    trace,
    debug,
    info,
    warn,
    error,
    fatal,

    trace_raw,
    debug_raw,
    info_raw,
    warn_raw,
    error_raw,
    fatal_raw,
};
})(globalThis);