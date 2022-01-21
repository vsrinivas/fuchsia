# FFX Plugin for Settings (SetUI)

This is an experimental plugin to modify and query settings. To use it, please opt in by running
`ffx config set setui true`.

## Development
* Create a new folder for a new settings as a subcommand of `setui`. (Similar to `setui/privacy`)
* Set build, for example:
```
fx set core.qemu-x64 --auto-dir --with src/developer/ffx:tests
```
* Build plugins:
```
fx build ffx
```
* Run unit tests:
```
fx test <new ffx_plugin target name>_test
```
* Run plugin:
```
fx ffx setui <new subcommand name>
```
