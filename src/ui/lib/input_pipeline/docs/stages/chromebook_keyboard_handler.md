# input_pipeline > Chromebook keyboard handler

In contrast to most "standard" PC laptop keyboards in which the top row keys are
dedicated to "function keys" such as F1, F2 etc., Chromebook keyboards have a
top row of "action" keys.

These keys invoke a specific action, such as the "Navigate back"
command in the browser, or actions to change screen brightness or play and
pause media.  The keyboard hardware reports these keys as function keys, F1 to
F10. But the physical keyboard layout dictates that these keys should be treated
as action keys by default.

This handler ensures that the keyboard behavior on Chromebooks running Fuchsia
matches the keyboard layout on the computer.  It translates the internal
key codes reported by the built-in keyboard into key codes matching those on
the keycaps of the Chromebook keyboard.

This behavior is only relevant for built-in laptop keyboards, and does not apply
to any external keyboards attached to the machine. External keyboards should
behave in the expected way.  For this reason, the key remapping only applies
to certain vendor ID and product ID combinations.

Since the top keyboard row is by default occupied by action keys, we must also
provide a different way to get the function key behavior as well. This is done
by actuating the top row of the keyboard together with the Search key (the key
occupying the position of the CapsLock on more standard keyboards).  The
Search key then acts as a modifier, and causes the F1 key event to be emitted
if the user presses Search+"Navigate back".  Similarly, all the other top row
keys are modified into respective function keys when used in a chord with
Search.

Since this handler remaps hardware key events, it should be hooked into the
input pipeline as soon as possible. This way the later pipeline handers can
deliberate based on the remapped hardware key codes, and we avoid handling
keymaps and key states again.

See the unit tests in `chromebook_keyboard_handler.rs` for the detailed
description of the behaviors that this handler implements.
