# Mozart Launch Tool

This directory contains Launch, a simple tool for running UI applications
which expose a ViewProvider.

Launch starts the specified application, requests its ViewProvider service,
creates a view, then asks the Presenter from the environment to display the
application.

This tool is primarily intended as scaffolding for running Mozart
applications while we build up the higher level parts of the system
which will eventually take over this function.

## USAGE

Run an application which offers a ViewProvider service:

    launch <app url> <app args>

### Arguments

Standard logging arguments:

    --verbose=[<level>]
    --quiet=[<level>]

### Examples

    launch noodles_view
