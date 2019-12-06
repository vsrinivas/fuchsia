// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import * as li_internal from 'li_internal';

(function(global) {
var readBuffer;
var repl;
var stdinFd;

// called when the shell is launched (with option -l)
// creates the C++ Repl object and sets the read handler on stdin
function InitRepl() {
  repl = li_internal.createRepl();
  stdinFd = std.in.fileno();

  global['repl'] = {
    evalScriptAwaitsPromise,
    repl,
  };

  // a handler on std.in
  readBuffer = new Uint8Array(64);
  os.setReadHandler(stdinFd, readHandler);
}

// Read handler, set on stdin by InitRepl
// Transfers all input to the C++ Repl object and deletes the Repl object when the \q exit
// command is detected in input
function readHandler() {
  var l = os.read(stdinFd, readBuffer.buffer, 0, readBuffer.length);
  var exitShell = li_internal.onInput(repl, readBuffer.buffer, l);
  if (exitShell) {
    std.out.flush();
    os.setReadHandler(stdinFd, null);  // remove read handler
    li_internal.closeRepl(repl);
  }
  std.gc();  // running it every now and then
}

// Called by the C++ Repl to evaluate commands
// Runs the command and if the result is a promise, sets showing the prompt again as callback
function evalScriptAwaitsPromise() {
  let eval_result = li_internal.getAndEvalCmd(repl);
  let error_in_script = eval_result[0];
  if (!error_in_script) {
    let script_result = eval_result[1];
    let f = function(output) {
      li_internal.showOutput(repl, output);
      li_internal.showPrompt(repl);
    };
    if (script_result instanceof Promise) {
      script_result.then(f).catch(f);
    } else {
      f(script_result);
    }
  } else {  // there was an error and it has been printed out by the getAndEvalCmd function
    li_internal.showPrompt(repl);
  };
}

InitRepl();
})(globalThis);