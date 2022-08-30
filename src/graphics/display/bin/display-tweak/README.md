# display-tweak - display driver debugging tool

`display-tweak` is a tool intended to help debug display drivers and hardware,
by tuning low-level parameters. The tool includes multiple commands, all of
which should have very straightforward mappings to FIDL calls into the display
driver system.

More complex testing code should be hosted in the `display-tool` sibling tool.
