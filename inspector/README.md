# Modular Inspector

The inspector allows developers to examine the Modular handler as it runs.
This includes access to:
 - the composition tree
 - active sessions
   - the session graph
   - modules and module instances

The inspector is implemented as a Polymer web app. It connects to a web
server inside the handler which exposes state as JSON over HTTP and WebSockets.
The front-end runs on port 8000, the handler's server runs on port 1842.

## Running

Just pass `--inspector` to `modular run`:

    modular run --inspector

Open the web interface at [http://localhost:8000/](http://localhost:8000/).

## Hacking

For deployment all dependencies referenced in `elements/dependencies.html` are
[vulcanized](https://github.com/Polymer/vulcanize) into
`elements/dependencies.vulcanized.html`. To actually be able to edit things
change the import in `elements/elements.html` to `dependencies.html`.

Use the `update-deps.sh` script to update the vulcanized version. It depends
on `bower` and `vulcanize` from `npm`.
