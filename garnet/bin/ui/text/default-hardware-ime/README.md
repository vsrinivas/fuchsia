# Default Hardware IME

A simple keymap-based hardware keyboard IME with dead keys and Unicode hex
input.

* Alt and Alt+Shift input non-ASCII characters.
* Alt+{`1234567890,./} select dead keys for {ˋ´˝˙¨˚ˆˇ˘˜¯¸˛⁄} respectively.
* Alt+Shift+0 initiates Unicode hex input. Enter terminates it.
* Keystrokes are determined by us.json.

(A future enhancement may allow for selecting layouts other than us.json,
such as uk.json, de.json, fr.json, etc.)

# This is not currently the actual default hardware IME!

Currently, the default IME is built into the ImeService, located in
`/garnet/bin/ui/ime/`.
