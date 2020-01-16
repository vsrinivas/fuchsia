# shell: Interact with Fuchsia by typing things.

This directory is for development of a Fuchsia shell.

A less generic name, and more content, is pending.

The current shell is based on JavaScript.

Currently, invoking FIDL calls (and wrappers around FIDL calls) requires
the use of `Promise`s.  You can invoke FIDL calls on protocols available from
`/svc` easily by saying something like:

```
svc.fuchsia_kernel_DebugBroker.SendDebugCommand("threadstats")
```

This returns a `Promise`.  Followup actions can be taken in the `Promise`'s then
clause.  You can also use `async`/`await`, although this is not supported for
direct invocation from the command line.

Arbitrary FIDL requests (those that do not depend on `/svc`) can be sent
using the `fidl.Request` and `fidl.ProtocolClient` API.  For example, if you have a
channel to a directory called dirChannel, and you want to open a path within that
directory, you can say:

```
let dirClient = new fidl.ProtocolClient(dirChannel, fidling.fuchsia_io.Directory);

const request = new fidl.Request(fidling.fuchsia_io.Node);
pathClient = request.getProtocolClient();
let openedPromise = pathClient.OnOpen((args) => {
  return args;
});
dirClient.Open(
    fidling.fuchsia_io.OPEN_RIGHT_READABLE | fidling.fuchsia_io.OPEN_FLAG_DESCRIBE, 0,
    path, request.getChannelForServer());
let args = await openedPromise;

// Manipulate args.s or args.info.

fidl.ProtocolClient.close(request.getProtocolClient());
```

Note that available FIDL libraries are available off of a global object called
`fidling`.  If a FIDL library you want is not available, please contact the OWNERS
of this directory.

If you want to try the shell, add the package "//src/developer/shell:josh" to
the packages available, shell into the device, and type "josh".

