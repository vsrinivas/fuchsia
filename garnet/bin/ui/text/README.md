# text

This directory contains system services for text input and editing. This document explains text input and gives on overview of Fuchsia's text input protocols, and describes the state of various text editing services on Fuchsia.

[TOC]

## What is text input?

Text input can seem like an afterthought to rendering a UI — just press characters on your keyboard, and they get inserted into a string. Text editing is more complex than it might appear at first glance. Here are some examples:

- **Caret Movement**: A caret is the indicator displayed on the screen that indicates where text input is inserted. Sometimes, this caret can move in unexpected ways. A text field has some wrapped monospaced text in it:

      Hello! This is a
      test of a text
      field.

  Assume that there is an input caret after the word "of". If you press up, the caret goes  to the start of "this". If you press up again, the caret goes to the start of “hello”.If you then press down, it goes to the end of “of”. This behavior may seem odd, but is a direct result of the same behavior that allows carets to jump back to the right if the user presses down and the caret passes through an empty line.

- **Japanese Input**: for many character based languages, the keyboard does not have nearly enough keys, and some users instead type how a word is pronounced (this text is underlined and usually called a *compose* region), and then the user selects the correct character from a list. In Japanese, this pronunciation is itself made up of other Japanese characters. On some mobile devices, there are buttons on a keyboard that allow highlighting just a prefix of the compose region to get suggestions just for that region.

- **Mixed Bidirectional Text**: if a text field includes the text "hello مرحبا hello" and then a user selects from the middle of one of the "hello"s to the middle of "مرحبا", the text selection can vary. This depends on if the user selected the text with the mouse or a keyboard and what platform they are using. Arrow-based cursor movement is a particularly gnarly problem.

## Glossary

- **Affinity**: Both with *soft-wrapped text* and with mixed *bidirectional text*, there are instances where a single offset into the text has two visual positions. For instance, on a soft-wrapped line break, the position before and the position after the break both have the same offset into the string. Text affinity disambiguates between those cases.
- **Anchor**: One of the two edges of a selection, the other being the *extent*. If the user is pressing shift, this edge holds still, and the mouse or the arrow keys only move the extent.
- **Backspace**: The button that deletes the current selection, or if the selection is a caret, deletes the grapheme behind the caret. In a right-to-left environment, this deletes towards the right, not the left, so if you're designing a keyboard, it is best practice to not draw an arrow on your backspace key. Although backspace usually deletes grapheme clusters, some *input methods* (such as Korean) that make a single grapheme out of several character presses  allow backspace to undo the previous character press if the selection hasn't moved.
- **Base**: Synonym of *anchor*.
- **Bidirectional Text**: Some languages, like Arabic and Hebrew, go from right to left instead of left to right. This leads to a lot of considerations, especially when left-to-right and right-to-left text is mixed on a single line. Some characters are even rendered differently based on their directionality. For example, the open parenthesis character flips horizontally.
- **Caret**: Synonym of *cursor*. Caret is sometimes preferred, since folks also use "cursor" to describe a mouse cursor.
- **Character**: Just like *left* and *right*, there is a more specific term to use. In C/ASCII it means a byte. In Java it means a *code unit*. In Rust it means a *scalar value*. In type design it means a *glyph*. To most people it means a *grapheme*.
- **CJK**: Chinese, Japanese, and Korean. All include Chinese characters or derivatives, which gives them unique challenges for input.
- **Code Point**: The 21-bit number that represents a character from the Unicode code space. UTF-8 represents a code point with 1–4 bytes, and UTF-16 represents a code point with 1 or 2 *code units*.
- **Code Unit**: The UTF-16 equivalent of bytes. Equal to two bytes.
- **Compose Region**: Some input methods, like Chinese Pinyin, allow the user to type a couple of characters that correspond to a character's pronunciation, and then the user can select the desired character from a drop-down list. This preview text is visualized on most systems by underlining the pronunciation. This composition region is used on some operating systems for non-*CJK* languages, to indicate which word is replaced in onscreen keyboards that provide suggestions, although this behavior is less common.
- **Cursor**: The blinking vertical bar that indicates where text appears when you type. Almost all text input frameworks internally represent this as a zero-width *selection*. The cursors does have a special case: if you press backspace with a selection, it deletes the selection, but if you press backspace with a cursor, it deletes the *grapheme* behind the cursor.
- **Dead Key**: A dead key is a key or key combination that, when pressed, modifies the subsequently pressed keys. For instance, on a Mac, pressing option+e inserts a highlighted `´` character, indicating that the next vowel pressed will replace this highlighted character, and be inserted with an accent. From the text field's perspective, the highlighted character is an arbitrary Unicode character for displaying to the user, and does not necessarily have any relation to the character that will replace it.
- **Extent**: One of the two edges of a selection, the other being the *anchor*. This is the edge that still moves when shift is pressed, either with the mouse or the arrow keys.
- **Focus**: In the context of a selection, synonym of *extent*. It's probably preferable to use extent, since focus also indicates the focused UI element, a related but separate concept.
- **Font**: A piece of software that describes how to draw some glyphs, and how text is translated into those glyphs. Distinct from a *typeface* in that a font has just a single weight and style.
- **Glyph**: The smallest atomic unit of writing in a text rendering system. This is distinct from a *grapheme* — glyphs are visual units, not logical units. For example, the text "fi" in many fonts is rendered as a single glyph, where the dot of the "i" is deleted, and replaced by extending the tip of the "f". This isn't just a squishing of the "f" and "i" glyphs, since the text rendering system sees "fi" as completely distinct from the "f" and "i" glyphs. However, "fi" is still two separate graphemes: you can select just "f" or "i" if you'd like, and semantically there is no difference between this glyph and if they were rendered using the normal "f" and "i" glyphs. Different fonts have different glyphs: "a" is a different glyph from "`a`". Most font software has a way to specify exactly where carets go when within a multi-grapheme glyph.
- **Grapheme**: The smallest atomic unit of writing in a writing system. A grapheme is a logical, conceptual unit: for example, "a" and "`a`" are both the same grapheme, even though they're different fonts, and may be drawn differently.
- **Grapheme Cluster**: A cluster of one or more code points that, together, make a single grapheme. For instance, the 👍 code point by itself is a first grapheme cluster, the 🏾 code point by itself is a second grapheme cluster, but if you put one after the other, the two codepoints form just one grapheme cluster: 👍🏾. This is the cluster that is usually deleted when a user presses backspace, and most systems only allow selection edges to appear on grapheme boundaries. Note, however, that there's an awkward possibility here: if the user selects the whitespace in the text "👍    🏾" and presses backspace, the two emoji graphemes clusters merge into a single cluster, leaving the resulting input caret halfway through the grapheme cluster. In instances like this, it's common to displace the input caret either upstream or downstream to the nearest grapheme boundary.
- **Hard-Wrapped Text**: An end of a line of text that is caused by a newline character.
- **Highlight Region**: Japanese input methods on mobile devices sometimes allow a prefix of the composition region to be highlighted. By default, the entire region is highlighted, but two arrow buttons allow moving the right edge of the highlight. The user only sees suggestions that complete the highlighted region, and pressing a suggestion re-highlights the unhighlighted suffix. This exists for users to type out many words at once, but then get suggestions for just a subset if the input method doesn't get all of the suggestions correctly
- **IME**: Stands for "input method editor" or "input method engine". This term sometimes ( [Wikipedia](https://en.wikipedia.org/wiki/Input_method)) denotes an *input method* that uses a technique to enter characters not printed on the physical keyboard, such as Chinese characters. However, even though Latin QWERTY keyboard layouts are not traditionally called an IME, modern layouts have *dead keys*, autocorrect, and may be an onscreen keyboard, so the distinction between input method and IME is not as meaningful now. Fuchsia uses the terms input method and IME interchangeably, but given the history of the term, consider using input method instead of IME, which is technically more generic.
- **Input Method**: Any piece of software that makes edits to the currently focused text field. An onscreen keyboard for Japanese is an input method. A voice transcription button is an input method. An English-language layout for physical keyboards is an input method, although instead of an onscreen app, it instead runs in the background, listening to physical keyboard events. An input method is the client of the input method protocol, the server being a *text field*.
- **Keymap**: Although setting the input method usually updates the keymap, it is still a distinct concept, used to determine if keyboard shortcuts are triggered, even on input methods that don't use the latin alphabet. Some input methods actually don't update the keymap. For instance, on some systems, setting the input method first to Dvorak and then to Chinese Pinyin results in a Pinyin-composing input method that types using a Dvorak keymap.
- **Left**: Are you sure left exists? RTL text is just as valid as LTR text, so "left" is often used when "upstream" or "before" would be more accurate. And don't forget mixed *bidirectional text* environments, where "upstream" can mean different directions in different places on the same line. That said, "left" still has meaning in certain cases. The left arrow key moves the cursor left visually, regardless of the directionality of the text, even in mixed bidirectional environments.
- **Right**: See *left*.
- **Scalar Value**: A subset of the Unicode *code point* space that does not include the code points reserved to represent surrogate pairs. These are the values that may be represented by a Rust `char`.
- **Selection**: Some text that is highlighted, usually with a blue background. Typing or pressing backspace deletes this text. Selections are usually created either with clicking and dragging, pressing shift and using the arrow keys, or pressing shift and clicking. Selections have two ends, either of which may come first: the *anchor* (also known as the *base*) and the *extent* (also known as the *focus*).
- **Soft-Wrapped Text**: When a single line of text is longer than the column it's in, some of the text wraps to the subsequent line, even if there aren't any newline characters. See *affinity* and *hard-wrapped text*.
- **Surrogate Pairs**: The UTF-16 character encoding sometimes needs two *code units* to represent a single *code point* — these two code units are called a surrogate pair. The pair of numbers that make up these surrogate pairs are actually part of the code point space, but are invalid code points in normal Unicode strings, since they would not be representable in UTF-16.
- **Text Field**: Generally, a box that you can type into. On Fuchsia, this term denotes any software that would like to receive text input, whether it's a box or not. A text field is the server of the input method protocol and the client is the *input method*.
- **Typeface**: A collection of *fonts*. For example, "Inconsolata" is a typeface, and "Inconsolata Bold" and "Inconsolata Regular" are fonts.

## Text editing overview

At a high level, there are a couple steps in how keypresses are usually converted into edits on most systems:

1. A user presses a key on the keyboard, which generates a HID report.
2. This HID report is optionally translated by a keymap, such as Dvorak or Colemak.
3. This translated keymap is tested to see if it matches any keyboard shortcuts; if so, it skips the remaining steps and just triggers that shortcut.
4. The key event is sent to input method (potentially written by a third-party). An input method could also expose an on-screen keyboard interface, in which case it would also receive touch or click events.
5. The input method combines these events with the latest state of the focused text field (necessary, for instance, to implement auto-correct) and issues a series of edit commands to the text field.

The systems in this directory are largely concerned with steps 4 and 5.

## Fuchsia's text input protocols

There are a number of FIDL protocols used by input methods and text input on Fuchsia.

- `ImeService`: This is a discoverable service for text fields; any text field can connect to this and request input. At some point in the future, this needs to be integrated this with Scenic's concept of focus, so that only the focused view is able to request text input.
- `ImeVisibilityService`: This is a discoverable service for shells. It simply tells the shell when the onscreen keyboard, if one exists, should be shown or hidden.
- Traditional input method APIs:
    - `InputMethodEditor and InputMethodEditorClient`: These are legacy APIs that simply encode a text field's entire state as a single FIDL struct. The input method and text field then just pass these FIDL structs back and forth to update each other. This is a pair of interfaces instead of a single interface since it was created before FIDL had events. As detailed in the next section, this API has race conditions. However, it is currently the only way for text fields to request input on Fuchsia.
- New input method APIs
    - `TextInputContext`: This is a discoverable service for input methods; any input method can connect to this and send edit commands. At some point in the future, this needs to be restricted, so that only trusted input method processes are allowed to send edits.
    - `TextField`: This is the API exposed to input methods through the `TextInputContext` service. A new one is sent for every newly focused text field. At some point in the future, a newly focused text field should be able to send a `TextField` interface to the `ImeService` instead of an `InputMethodEditorClient`. `ImeService` is able to translate an input method's edits sent with the `TextField` interface through to legacy text fields that use `InputMethodEditorClient`. 
    - `TextFieldTestSuite`: If input methods and text fields each implement the `TextField` protocol slightly differently, a new participant in the protocol must test their implementation against many others to make sure it works properly. There is a standard `TestField` test suite to avoid the n^2 manual tests that result from this. `TextFieldTestSuite` has two methods: one lists tests, and another runs a specified test on a given `TextField`. `TextField` implementations should implement tests that run `TestFieldTestSuite` against themselves.

## Fuchsia's various text services and projects

- `garnet/bin/ui/ime`
    - The IME Service is the central organizer of text input on Fuchsia. It vends the discoverable `ImeService`, `ImeVisibilityService`, and `TextInputContext` interfaces. It also contains a legacy IME module that serves the `TextField` and `InputMethodEditor` interfaces. When a text field is focused through `ImeService.GetInputMethodEditor()`, the InputMethodEditor/InputMethodEditorClient pair is passed to a new instance of the legacy IME using `bind_ime()`. The legacy IME is then able to send state updates to the client either in response to key events with `inject_input()` (using an internal default input method) or in response to edits from a connected v2-style input method, which connects to `TextInputContext` and makes changes through the `TextField` interface.
- `garnet/bin/ui/text/test_suite`
    - The component that serves the `TextFieldTestSuite` interface.
- `garnet/bin/ui/text/default_hardware_ime`
    - An input method that inserts QWERTY, latin characters. It also has systems for inserting special characters with dead keys. It is not actually spun up by default, but if it is, input will flow through this instead of through `legacy_ime`'s `inject_input()` method.
- `topaz/app/latin-ime`
    - An onscreen keyboard. Currently just passes events through `InjectInput()` on `ImeService`, but should be upgraded to a proper `TextField`-powered input method in the future, like `default_hardware_ime`.
- `sdk/fidl/fuchsia.ui.text`
    - Holds the `TextField` protocol, and other related v2 input method protocols and structs.
- `sdk/fidl/fuchsia.ui.text.testing`
    - Holds the `TextFieldTestSuite` protocol and related structs.
- `garnet/lib/ui/text/common`
    - Helper functions and structs used across the various text projects. 
- `topaz/bin/xi`
    - Binaries for the Xi editor.
- `topaz/lib/xi`
    - Common libraries for the Xi editor.
