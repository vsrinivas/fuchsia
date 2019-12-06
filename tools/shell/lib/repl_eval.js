// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import * as li_internal from 'li_internal';

(function (global) {
  function evalScriptAwaitsPromise() {
    let eval_result = li_internal.getAndEvalCmd(repl_init.repl);
    let error_in_script = eval_result[0];
    if (!error_in_script) {
      let script_result = eval_result[1];
      let f = function (output) {
        li_internal.showOutput(repl_init.repl, output);
        li_internal.showPrompt(repl_init.repl); 
      };
      if (script_result instanceof Promise) {
        script_result.then(f).catch(f);
      }
      else {
        f(script_result);
      }
    }
    else {//there was an error and it has been printed out by the getAndEvalCmd function
      li_internal.showPrompt(repl_init.repl);
    };
  }

  global['repl_eval'] = {
    evalScriptAwaitsPromise,
  };

})(globalThis);