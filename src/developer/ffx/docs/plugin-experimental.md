# Experimental plugins

When developing a new plugin, the experimental (or 'work in progress') nature of
the work is indicated by marking the plugin as experimental.

## Mark a plugin as experimental

When defining a plugin function within the plugin lib.rs file, there will be an
entry point decorated with `ffx_plugin` such as:

```
#[ffx_plugin()]
```

or

```
#[ffx_plugin(MyProxy = "fuchsia.example.Service")]
```

To mark a plugin as experimental and simultaneously declare the token used to
enable the experiment, add an initial string parameter. In the following example
the tag "zippy" may (or may not) match the plugin command name. The label may be
arbitrary, but it's recommended to choose a value that relates to the plugin:

```
#[ffx_plugin("zippy")]
```

or

```
#[ffx_plugin("zippy", MyProxy = "fuchsia.example.Service")]
```

In both examples above, the plugin will be guarded by a feature token named
"zippy". When users try to execute `ffx zippy` they will be informed that zippy
is experimental, then instructions for enabling the zippy feature will be shown.

After following the instructions to enable zippy, future calls (for that user)
will operate as if zippy were not experimental, i.e. they have opted-in to using
the zippy feature.
