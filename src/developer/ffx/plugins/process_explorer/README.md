# FFX Plugin to query process information

The process_explorer plugin outputs information such as existing channels,
events, sockets, etc. for all processes on a running Fuchsia device. The
information is currently output in JSON format, future improvements of the
plugin include subcommands that will allow data filtering and displaying
it in a more readable way.

## Development

* To set the build, consider using:
```
fx set [...] --with //sdk/fidl/fuchsia.process.explorer
```
* To run the plugin:
```
ffx process_explorer
```
The output of the plugin, has the following structure:
 ```
    {
        "Processes":[
            {
                "koid":1097,
                "name":"bin/component_manager",
                "objects":[
                    {
                        "type":17,
                        "koid":41903,
                        "related_koid":1033,
                        "peer_owner_koid":0
                    },
                    ...
                ]
            },
            ...
        ]
    }
```