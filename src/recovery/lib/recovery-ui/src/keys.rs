// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(PartialEq, Debug)]
pub struct LetterKey {
    pub(crate) lower: &'static str,
    pub(crate) upper: &'static str,
    pub(crate) alt: &'static str,
    pub(crate) is_alt_accent: bool,
}

#[derive(PartialEq, Debug)]
pub enum SpecialKey {
    ALT,
    DEL,
    ENTER,
    SHIFT,
    SPACE,
}

#[derive(PartialEq, Debug)]
pub enum Key {
    Letter(LetterKey),
    Special(SpecialKey, &'static str),
}

const DEFAULT_KEY: LetterKey = LetterKey { lower: "", upper: "", alt: "", is_alt_accent: false };

const KEY_A: Key = Key::Letter(LetterKey { lower: "a", upper: "A", alt: "æ", ..DEFAULT_KEY });
const KEY_B: Key = Key::Letter(LetterKey { lower: "b", upper: "B", ..DEFAULT_KEY });
const KEY_C: Key = Key::Letter(LetterKey { lower: "c", upper: "C", alt: "ç", ..DEFAULT_KEY });
const KEY_D: Key = Key::Letter(LetterKey { lower: "d", upper: "D", alt: "∂", ..DEFAULT_KEY });
const KEY_E: Key =
    Key::Letter(LetterKey {
        lower: "e", upper: "E", alt: "´", is_alt_accent: true, ..DEFAULT_KEY
    });
const KEY_F: Key = Key::Letter(LetterKey { lower: "f", upper: "F", alt: "ƒ", ..DEFAULT_KEY });
const KEY_G: Key = Key::Letter(LetterKey { lower: "g", upper: "G", alt: "©", ..DEFAULT_KEY });
const KEY_H: Key = Key::Letter(LetterKey { lower: "h", upper: "H", ..DEFAULT_KEY });
const KEY_I: Key =
    Key::Letter(LetterKey {
        lower: "i", upper: "I", alt: "ˆ", is_alt_accent: true, ..DEFAULT_KEY
    });
const KEY_J: Key = Key::Letter(LetterKey { lower: "j", upper: "J", ..DEFAULT_KEY });
const KEY_K: Key =
    Key::Letter(LetterKey {
        lower: "k", upper: "K", alt: "˚", is_alt_accent: true, ..DEFAULT_KEY
    });
const KEY_L: Key = Key::Letter(LetterKey { lower: "l", upper: "L", ..DEFAULT_KEY });
const KEY_M: Key = Key::Letter(LetterKey { lower: "m", upper: "M", ..DEFAULT_KEY });
const KEY_N: Key =
    Key::Letter(LetterKey {
        lower: "n", upper: "N", alt: "˜", is_alt_accent: true, ..DEFAULT_KEY
    });
const KEY_O: Key = Key::Letter(LetterKey { lower: "o", upper: "O", alt: "ø", ..DEFAULT_KEY });
const KEY_P: Key = Key::Letter(LetterKey { lower: "p", upper: "P", alt: "Ø", ..DEFAULT_KEY });
const KEY_Q: Key = Key::Letter(LetterKey { lower: "q", upper: "Q", alt: "œ", ..DEFAULT_KEY });
const KEY_R: Key = Key::Letter(LetterKey { lower: "r", upper: "R", alt: "®", ..DEFAULT_KEY });
const KEY_S: Key = Key::Letter(LetterKey { lower: "s", upper: "S", alt: "ß", ..DEFAULT_KEY });
const KEY_T: Key = Key::Letter(LetterKey { lower: "t", upper: "T", ..DEFAULT_KEY });
const KEY_U: Key =
    Key::Letter(LetterKey {
        lower: "u", upper: "U", alt: "¨", is_alt_accent: true, ..DEFAULT_KEY
    });
const KEY_V: Key = Key::Letter(LetterKey { lower: "v", upper: "V", ..DEFAULT_KEY });
const KEY_W: Key = Key::Letter(LetterKey { lower: "w", upper: "W", alt: "Œ", ..DEFAULT_KEY });
const KEY_X: Key = Key::Letter(LetterKey { lower: "x", upper: "X", alt: "Ç", ..DEFAULT_KEY });
const KEY_Y: Key = Key::Letter(LetterKey { lower: "y", upper: "Y", alt: "¥", ..DEFAULT_KEY });
const KEY_Z: Key = Key::Letter(LetterKey { lower: "z", upper: "Z", alt: "Æ", ..DEFAULT_KEY });
const KEY_0: Key = Key::Letter(LetterKey { lower: "0", upper: ")", alt: "º", ..DEFAULT_KEY });
const KEY_1: Key = Key::Letter(LetterKey { lower: "1", upper: "!", alt: "¡", ..DEFAULT_KEY });
const KEY_2: Key = Key::Letter(LetterKey { lower: "2", upper: "@", alt: "€", ..DEFAULT_KEY });
const KEY_3: Key = Key::Letter(LetterKey { lower: "3", upper: "#", alt: "£", ..DEFAULT_KEY });
const KEY_4: Key = Key::Letter(LetterKey { lower: "4", upper: "$", alt: "¢", ..DEFAULT_KEY });
const KEY_5: Key = Key::Letter(LetterKey { lower: "5", upper: "%", alt: "∞", ..DEFAULT_KEY });
const KEY_6: Key = Key::Letter(LetterKey { lower: "6", upper: "^", alt: "§", ..DEFAULT_KEY });
const KEY_7: Key = Key::Letter(LetterKey { lower: "7", upper: "&", alt: "¶", ..DEFAULT_KEY });
const KEY_8: Key = Key::Letter(LetterKey { lower: "8", upper: "*", alt: "•", ..DEFAULT_KEY });
const KEY_9: Key = Key::Letter(LetterKey { lower: "9", upper: "(", alt: "ª", ..DEFAULT_KEY });
const KEY_BACK_TICK: Key =
    Key::Letter(LetterKey { lower: "`", upper: "~", alt: "`", is_alt_accent: true, ..DEFAULT_KEY });
const KEY_HYPHEN: Key = Key::Letter(LetterKey { lower: "-", upper: "_", alt: "±", ..DEFAULT_KEY });
const KEY_EQUALS: Key =
    Key::Letter(LetterKey { lower: "=", upper: "+", alt: "≠", ..DEFAULT_KEY });
const KEY_L_BRACKET: Key = Key::Letter(LetterKey { lower: "[", upper: "{", ..DEFAULT_KEY });
const KEY_R_BRACKET: Key = Key::Letter(LetterKey { lower: "]", upper: "}", ..DEFAULT_KEY });
const KEY_BACKSLASH: Key = Key::Letter(LetterKey { lower: "\\", upper: "|", ..DEFAULT_KEY });
const KEY_SEMICOLON: Key = Key::Letter(LetterKey { lower: ";", upper: ":", ..DEFAULT_KEY });
const KEY_QUOTE: Key = Key::Letter(LetterKey { lower: "'", upper: "\"", ..DEFAULT_KEY });
const KEY_COMMA: Key = Key::Letter(LetterKey { lower: ",", upper: "<", ..DEFAULT_KEY });
const KEY_FULL_STOP: Key = Key::Letter(LetterKey { lower: ".", upper: ">", ..DEFAULT_KEY });
const KEY_SLASH: Key = Key::Letter(LetterKey { lower: "/", upper: "?", ..DEFAULT_KEY });

const KEY_ALT: Key = Key::Special(SpecialKey::ALT, "alt");
const KEY_DEL: Key = Key::Special(SpecialKey::DEL, "del");
const KEY_ENTER: Key = Key::Special(SpecialKey::ENTER, "enter");
const KEY_SHIFT: Key = Key::Special(SpecialKey::SHIFT, "shift");
const KEY_SPACE: Key = Key::Special(SpecialKey::SPACE, "space");

pub(crate) const ROW0: &'static [&Key] = &[
    &KEY_BACK_TICK,
    &KEY_1,
    &KEY_2,
    &KEY_3,
    &KEY_4,
    &KEY_5,
    &KEY_6,
    &KEY_7,
    &KEY_8,
    &KEY_9,
    &KEY_0,
    &KEY_HYPHEN,
    &KEY_EQUALS,
];
pub(crate) const ROW1: &'static [&Key] = &[
    &KEY_Q,
    &KEY_W,
    &KEY_E,
    &KEY_R,
    &KEY_T,
    &KEY_Y,
    &KEY_U,
    &KEY_I,
    &KEY_O,
    &KEY_P,
    &KEY_L_BRACKET,
    &KEY_R_BRACKET,
    &KEY_BACKSLASH,
];
const ROW2: &'static [&Key] = &[
    &KEY_A,
    &KEY_S,
    &KEY_D,
    &KEY_F,
    &KEY_G,
    &KEY_H,
    &KEY_J,
    &KEY_K,
    &KEY_L,
    &KEY_SEMICOLON,
    &KEY_QUOTE,
];
const ROW3: &'static [&Key] = &[
    &KEY_Z,
    &KEY_X,
    &KEY_C,
    &KEY_V,
    &KEY_B,
    &KEY_N,
    &KEY_M,
    &KEY_COMMA,
    &KEY_FULL_STOP,
    &KEY_SLASH,
];
const SPECIAL_ROW: &'static [&Key] = &[&KEY_SHIFT, &KEY_ALT, &KEY_SPACE, &KEY_DEL, &KEY_ENTER];

pub const KEYBOARD: &'static [&'static [&Key]] = &[ROW0, ROW1, ROW2, ROW3, SPECIAL_ROW];

#[derive(Debug)]
pub struct Accent {
    accent: &'static Key,
    pub(crate) lower: &'static str,
    pub(crate) upper: &'static str,
}

#[derive(Debug)]
pub struct AccentKey {
    pub alt_key: &'static Key,
    accents: &'static [&'static Accent],
}

const ACCENT_GRAVE: AccentKey = AccentKey {
    alt_key: &KEY_BACK_TICK,
    accents: &[
        &Accent { accent: &KEY_A, lower: "à", upper: "À" },
        &Accent { accent: &KEY_E, lower: "è", upper: "È" },
        &Accent { accent: &KEY_I, lower: "ì", upper: "Ì" },
        &Accent { accent: &KEY_O, lower: "ò", upper: "Ò" },
        &Accent { accent: &KEY_U, lower: "ù", upper: "Ù" },
    ],
};

const ACCENT_ACUTE: AccentKey = AccentKey {
    alt_key: &KEY_E,
    accents: &[
        &Accent { accent: &KEY_A, lower: "á", upper: "Á" },
        &Accent { accent: &KEY_E, lower: "é", upper: "É" },
        &Accent { accent: &KEY_I, lower: "í", upper: "Í" },
        &Accent { accent: &KEY_O, lower: "ó", upper: "Ó" },
        &Accent { accent: &KEY_U, lower: "ú", upper: "Ú" },
    ],
};

const ACCENT_RING: AccentKey =
    AccentKey { alt_key: &KEY_K, accents: &[&Accent { accent: &KEY_A, lower: "å", upper: "Å" }] };

const ACCENT_UMLAUT: AccentKey = AccentKey {
    alt_key: &KEY_U,
    accents: &[
        &Accent { accent: &KEY_A, lower: "ä", upper: "Ä" },
        &Accent { accent: &KEY_E, lower: "ë", upper: "Ë" },
        &Accent { accent: &KEY_I, lower: "ï", upper: "Ï" },
        &Accent { accent: &KEY_O, lower: "ö", upper: "Ö" },
        &Accent { accent: &KEY_U, lower: "ü", upper: "ü" },
    ],
};

const ACCENT_CIRCUMFLEX: AccentKey = AccentKey {
    alt_key: &KEY_I,
    accents: &[
        &Accent { accent: &KEY_A, lower: "â", upper: "Â" },
        &Accent { accent: &KEY_E, lower: "ê", upper: "Ê" },
        &Accent { accent: &KEY_I, lower: "î", upper: "Î" },
        &Accent { accent: &KEY_O, lower: "ô", upper: "Ô" },
        &Accent { accent: &KEY_U, lower: "û", upper: "Û" },
    ],
};

const ACCENT_TILDE: AccentKey = AccentKey {
    alt_key: &KEY_N,
    accents: &[
        &Accent { accent: &KEY_A, lower: "ã", upper: "Ã" },
        &Accent { accent: &KEY_N, lower: "ñ", upper: "Ñ" },
        &Accent { accent: &KEY_O, lower: "õ", upper: "Õ	" },
    ],
};

pub const ACCENTS: &'static [AccentKey] =
    &[ACCENT_GRAVE, ACCENT_CIRCUMFLEX, ACCENT_ACUTE, ACCENT_RING, ACCENT_UMLAUT, ACCENT_TILDE];

pub fn get_accent(alt_key: &Key, pressed_key: &Key) -> Option<&'static &'static Accent> {
    if let Some(accent_key) = ACCENTS.iter().find(|ak| ak.alt_key == alt_key) {
        return accent_key.accents.iter().find(|a| a.accent == pressed_key);
    };
    None
}
