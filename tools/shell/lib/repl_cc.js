// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import * as li_internal from 'li_internal';

(function() {
    var readBuffer;
    var repl;
    var stdinFd;

    function InitRepl() {
        repl = li_internal.createRepl();
        stdinFd = std.in.fileno();

        // a handler on std.in
        readBuffer = new Uint8Array(64);
        os.setReadHandler(stdinFd, readHandler);
    }

    function readHandler() {
        var l = os.read(stdinFd, readBuffer.buffer, 0, readBuffer.length);
        var exitShell =li_internal.onInput(repl, readBuffer.buffer, l);
        if(exitShell){
            os.setReadHandler(stdinFd, null); //remove read handler
            li_internal.closeRepl(repl);
        }
        std.gc();//running it every now and then
    }

    InitRepl();

})();
