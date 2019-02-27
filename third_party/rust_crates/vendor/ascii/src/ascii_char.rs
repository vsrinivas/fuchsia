// #[allow(deprecated)] doesn't silence warnings on the method invocations,
// which would call the inherent methods if AsciiExt wasn't in scope.
#![cfg_attr(feature = "std", allow(deprecated))]

#[cfg(feature = "quickcheck")]
use quickcheck::{Arbitrary, Gen};

use core::mem;
use core::cmp::Ordering;
use core::{fmt, char};
#[cfg(feature = "std")]
use std::error::Error;
#[cfg(feature = "std")]
use std::ascii::AsciiExt;

#[allow(non_camel_case_types)]
/// An ASCII character. It wraps a `u8`, with the highest bit always zero.
#[derive(Clone, PartialEq, PartialOrd, Ord, Eq, Hash, Copy)]
#[repr(u8)]
pub enum AsciiChar {
    /// `'\0'`
    Null = 0,
    /// [Start Of Heading](http://en.wikipedia.org/wiki/Start_of_Heading)
    SOH = 1,
    /// [Start Of teXt](http://en.wikipedia.org/wiki/Start_of_Text)
    SOX = 2,
    /// [End of TeXt](http://en.wikipedia.org/wiki/End-of-Text_character)
    ETX = 3,
    /// [End Of Transmission](http://en.wikipedia.org/wiki/End-of-Transmission_character)
    EOT = 4,
    /// [Enquiry](http://en.wikipedia.org/wiki/Enquiry_character)
    ENQ = 5,
    /// [Acknowledgement](http://en.wikipedia.org/wiki/Acknowledge_character)
    ACK = 6,
    /// [bell / alarm / audible](http://en.wikipedia.org/wiki/Bell_character)
    ///
    /// `'\a'` is not recognized by Rust.
    Bell = 7,
    /// [Backspace](http://en.wikipedia.org/wiki/Backspace)
    ///
    /// `'\b'` is not recognized by Rust.
    BackSpace = 8,
    /// `'\t'`
    Tab = 9,
    /// `'\n'`
    LineFeed = 10,
    /// [Vertical tab](http://en.wikipedia.org/wiki/Vertical_Tab)
    ///
    /// `'\v'` is not recognized by Rust.
    VT = 11,
    /// [Form Feed](http://en.wikipedia.org/wiki/Form_Feed)
    ///
    /// `'\f'` is not recognized by Rust.
    FF = 12,
    /// `'\r'`
    CarriageReturn = 13,
    /// [Shift In](http://en.wikipedia.org/wiki/Shift_Out_and_Shift_In_characters)
    SI = 14,
    /// [Shift Out](http://en.wikipedia.org/wiki/Shift_Out_and_Shift_In_characters)
    SO = 15,
    /// [Data Link Escape](http://en.wikipedia.org/wiki/Data_Link_Escape)
    DLE = 16,
    /// [Device control 1, often XON](http://en.wikipedia.org/wiki/Device_Control_1)
    DC1 = 17,
    /// Device control 2
    DC2 = 18,
    /// Device control 3, Often XOFF
    DC3 = 19,
    /// Device control 4
    DC4 = 20,
    /// [Negative AcKnowledgement](http://en.wikipedia.org/wiki/Negative-acknowledge_character)
    NAK = 21,
    /// [Synchronous idle](http://en.wikipedia.org/wiki/Synchronous_Idle)
    SYN = 22,
    /// [End of Transmission Block](http://en.wikipedia.org/wiki/End-of-Transmission-Block_character)
    ETB = 23,
    /// [Cancel](http://en.wikipedia.org/wiki/Cancel_character)
    CAN = 24,
    /// [End of Medium](http://en.wikipedia.org/wiki/End_of_Medium)
    EM = 25,
    /// [Substitute](http://en.wikipedia.org/wiki/Substitute_character)
    SUB = 26,
    /// [Escape](http://en.wikipedia.org/wiki/Escape_character)
    ///
    /// `'\e'` is not recognized by Rust.
    ESC = 27,
    /// [File Separator](http://en.wikipedia.org/wiki/File_separator)
    FS = 28,
    /// [Group Separator](http://en.wikipedia.org/wiki/Group_separator)
    GS = 29,
    /// [Record Separator](http://en.wikipedia.org/wiki/Record_separator)
    RS = 30,
    /// [Unit Separator](http://en.wikipedia.org/wiki/Unit_separator)
    US = 31,
    /// `' '`
    Space = 32,
    /// `'!'`
    Exclamation = 33,
    /// `'"'`
    Quotation = 34,
    /// `'#'`
    Hash = 35,
    /// `'$'`
    Dollar = 36,
    /// `'%'`
    Percent = 37,
    /// `'&'`
    Ampersand = 38,
    /// `'\''`
    Apostrophe = 39,
    /// `'('`
    ParenOpen = 40,
    /// `')'`
    ParenClose = 41,
    /// `'*'`
    Asterisk = 42,
    /// `'+'`
    Plus = 43,
    /// `','`
    Comma = 44,
    /// `'-'`
    Minus = 45,
    /// `'.'`
    Dot = 46,
    /// `'/'`
    Slash = 47,
    /// `'0'`
    _0 = 48,
    /// `'1'`
    _1 = 49,
    /// `'2'`
    _2 = 50,
    /// `'3'`
    _3 = 51,
    /// `'4'`
    _4 = 52,
    /// `'5'`
    _5 = 53,
    /// `'6'`
    _6 = 54,
    /// `'7'`
    _7 = 55,
    /// `'8'`
    _8 = 56,
    /// `'9'`
    _9 = 57,
    /// `':'`
    Colon = 58,
    /// `';'`
    Semicolon = 59,
    /// `'<'`
    LessThan = 60,
    /// `'='`
    Equal = 61,
    /// `'>'`
    GreaterThan = 62,
    /// `'?'`
    Question = 63,
    /// `'@'`
    At = 64,
    /// `'A'`
    A = 65,
    /// `'B'`
    B = 66,
    /// `'C'`
    C = 67,
    /// `'D'`
    D = 68,
    /// `'E'`
    E = 69,
    /// `'F'`
    F = 70,
    /// `'G'`
    G = 71,
    /// `'H'`
    H = 72,
    /// `'I'`
    I = 73,
    /// `'J'`
    J = 74,
    /// `'K'`
    K = 75,
    /// `'L'`
    L = 76,
    /// `'M'`
    M = 77,
    /// `'N'`
    N = 78,
    /// `'O'`
    O = 79,
    /// `'P'`
    P = 80,
    /// `'Q'`
    Q = 81,
    /// `'R'`
    R = 82,
    /// `'S'`
    S = 83,
    /// `'T'`
    T = 84,
    /// `'U'`
    U = 85,
    /// `'V'`
    V = 86,
    /// `'W'`
    W = 87,
    /// `'X'`
    X = 88,
    /// `'Y'`
    Y = 89,
    /// `'Z'`
    Z = 90,
    /// `'['`
    BracketOpen = 91,
    /// `'\'`
    BackSlash = 92,
    /// `']'`
    BracketClose = 93,
    /// `'_'`
    Caret = 94,
    /// `'_'`
    UnderScore = 95,
    /// `'`'`
    Grave = 96,
    /// `'a'`
    a = 97,
    /// `'b'`
    b = 98,
    /// `'c'`
    c = 99,
    /// `'d'`
    d = 100,
    /// `'e'`
    e = 101,
    /// `'f'`
    f = 102,
    /// `'g'`
    g = 103,
    /// `'h'`
    h = 104,
    /// `'i'`
    i = 105,
    /// `'j'`
    j = 106,
    /// `'k'`
    k = 107,
    /// `'l'`
    l = 108,
    /// `'m'`
    m = 109,
    /// `'n'`
    n = 110,
    /// `'o'`
    o = 111,
    /// `'p'`
    p = 112,
    /// `'q'`
    q = 113,
    /// `'r'`
    r = 114,
    /// `'s'`
    s = 115,
    /// `'t'`
    t = 116,
    /// `'u'`
    u = 117,
    /// `'v'`
    v = 118,
    /// `'w'`
    w = 119,
    /// `'x'`
    x = 120,
    /// `'y'`
    y = 121,
    /// `'z'`
    z = 122,
    /// `'{'`
    CurlyBraceOpen = 123,
    /// `'|'`
    VerticalBar = 124,
    /// `'}'`
    CurlyBraceClose = 125,
    /// `'~'`
    Tilde = 126,
    /// [Delete](http://en.wikipedia.org/wiki/Delete_character)
    DEL = 127,
}

impl AsciiChar {
    /// Constructs an ASCII character from a `u8`, `char` or other character type.
    ///
    /// # Failure
    /// Returns `Err(())` if the character can't be ASCII encoded.
    ///
    /// # Example
    /// ```
    /// # use ascii::AsciiChar;
    /// let a = AsciiChar::from('g').unwrap();
    /// assert_eq!(a.as_char(), 'g');
    /// ```
    #[inline]
    pub fn from<C: ToAsciiChar>(ch: C) -> Result<Self, ToAsciiCharError> {
        ch.to_ascii_char()
    }

    /// Constructs an ASCII character from a `char` or `u8` without any checks.
    #[inline]
    pub unsafe fn from_unchecked<C: ToAsciiChar>(ch: C) -> Self {
        ch.to_ascii_char_unchecked()
    }

    /// Converts an ASCII character into a `u8`.
    #[inline]
    pub fn as_byte(self) -> u8 {
        self as u8
    }

    /// Converts an ASCII character into a `char`.
    #[inline]
    pub fn as_char(self) -> char {
        self.as_byte() as char
    }

    // the following methods are like ctype, and the implementation is inspired by musl

    /// Check if the character is a letter (a-z, A-Z)
    #[inline]
    pub fn is_alphabetic(self) -> bool {
        let c = self.as_byte() | 0b010_0000; // Turns uppercase into lowercase.
        c >= b'a' && c <= b'z'
    }

    /// Check if the character is a number (0-9)
    #[inline]
    pub fn is_digit(self) -> bool {
        self >= AsciiChar::_0 && self <= AsciiChar::_9
    }

    /// Check if the character is a letter or number
    #[inline]
    pub fn is_alphanumeric(self) -> bool {
        self.is_alphabetic() || self.is_digit()
    }

    /// Check if the character is a space or horizontal tab
    #[inline]
    pub fn is_blank(self) -> bool {
        self == AsciiChar::Space || self == AsciiChar::Tab
    }

    /// Check if the character is a ' ', '\t', '\n' or '\r'
    #[inline]
    pub fn is_whitespace(self) -> bool {
        self.is_blank() || self == AsciiChar::LineFeed || self == AsciiChar::CarriageReturn
    }

    /// Check if the character is a control character
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('\0'.to_ascii_char().unwrap().is_control(), true);
    /// assert_eq!('n'.to_ascii_char().unwrap().is_control(), false);
    /// assert_eq!(' '.to_ascii_char().unwrap().is_control(), false);
    /// assert_eq!('\n'.to_ascii_char().unwrap().is_control(), true);
    /// ```
    #[inline]
    pub fn is_control(self) -> bool {
        self < AsciiChar::Space || self == AsciiChar::DEL
    }

    /// Checks if the character is printable (except space)
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('n'.to_ascii_char().unwrap().is_graph(), true);
    /// assert_eq!(' '.to_ascii_char().unwrap().is_graph(), false);
    /// assert_eq!('\n'.to_ascii_char().unwrap().is_graph(), false);
    /// ```
    #[inline]
    pub fn is_graph(self) -> bool {
        self.as_byte().wrapping_sub(b' ' + 1) < 0x5E
    }

    /// Checks if the character is printable (including space)
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('n'.to_ascii_char().unwrap().is_print(), true);
    /// assert_eq!(' '.to_ascii_char().unwrap().is_print(), true);
    /// assert_eq!('\n'.to_ascii_char().unwrap().is_print(), false);
    /// ```
    #[inline]
    pub fn is_print(self) -> bool {
        self.as_byte().wrapping_sub(b' ') < 0x5F
    }

    /// Checks if the character is alphabetic and lowercase
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('a'.to_ascii_char().unwrap().is_lowercase(), true);
    /// assert_eq!('A'.to_ascii_char().unwrap().is_lowercase(), false);
    /// assert_eq!('@'.to_ascii_char().unwrap().is_lowercase(), false);
    /// ```
    #[inline]
    pub fn is_lowercase(self) -> bool {
        self.as_byte().wrapping_sub(b'a') < 26
    }

    /// Checks if the character is alphabetic and uppercase
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('A'.to_ascii_char().unwrap().is_uppercase(), true);
    /// assert_eq!('a'.to_ascii_char().unwrap().is_uppercase(), false);
    /// assert_eq!('@'.to_ascii_char().unwrap().is_uppercase(), false);
    /// ```
    #[inline]
    pub fn is_uppercase(self) -> bool {
        self.as_byte().wrapping_sub(b'A') < 26
    }

    /// Checks if the character is punctuation
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('n'.to_ascii_char().unwrap().is_punctuation(), false);
    /// assert_eq!(' '.to_ascii_char().unwrap().is_punctuation(), false);
    /// assert_eq!('_'.to_ascii_char().unwrap().is_punctuation(), true);
    /// assert_eq!('~'.to_ascii_char().unwrap().is_punctuation(), true);
    /// ```
    #[inline]
    pub fn is_punctuation(self) -> bool {
        self.is_graph() && !self.is_alphanumeric()
    }

    /// Checks if the character is a valid hex digit
    ///
    /// # Examples
    /// ```
    /// use ascii::ToAsciiChar;
    /// assert_eq!('5'.to_ascii_char().unwrap().is_hex(), true);
    /// assert_eq!('a'.to_ascii_char().unwrap().is_hex(), true);
    /// assert_eq!('F'.to_ascii_char().unwrap().is_hex(), true);
    /// assert_eq!('G'.to_ascii_char().unwrap().is_hex(), false);
    /// assert_eq!(' '.to_ascii_char().unwrap().is_hex(), false);
    /// ```
    #[inline]
    pub fn is_hex(self) -> bool {
        self.is_digit() || (self.as_byte() | 0x20u8).wrapping_sub(b'a') < 6
    }

    /// Unicode has printable versions of the ASCII control codes, like '␛'.
    ///
    /// This function is identical with `.as_char()`
    /// for all values `.is_printable()` returns true for,
    /// but replaces the control codes with those unicodes printable versions.
    ///
    /// # Examples
    /// ```
    /// # use ascii::ToAsciiChar;
    /// assert_eq!('\0'.to_ascii_char().unwrap().as_printable_char(), '␀');
    /// assert_eq!('\n'.to_ascii_char().unwrap().as_printable_char(), '␊');
    /// assert_eq!(' '.to_ascii_char().unwrap().as_printable_char(), ' ');
    /// assert_eq!('p'.to_ascii_char().unwrap().as_printable_char(), 'p');
    /// ```
    pub fn as_printable_char(self) -> char {
        unsafe {
            match self as u8 {
                b' '...b'~' => self.as_char(),
                127 => '␡',
                _ => char::from_u32_unchecked(self as u32 + '␀' as u32),
            }
        }
    }

    /// Replaces letters `a` to `z` with `A` to `Z`
    pub fn make_ascii_uppercase(&mut self) {
        *self = self.to_ascii_uppercase()
    }

    /// Replaces letters `A` to `Z` with `a` to `z`
    pub fn make_ascii_lowercase(&mut self) {
        *self = self.to_ascii_lowercase()
    }

    /// Maps letters `a`...`z` to `A`...`Z` and returns everything else unchanged.
    #[inline]
    pub fn to_ascii_uppercase(&self) -> Self {
        unsafe {
            match *self as u8 {
                b'a'...b'z' => AsciiChar::from_unchecked(self.as_byte() - (b'a' - b'A')),
                _ => *self,
            }
        }
    }

    /// Maps letters `A`...`Z` to `a`...`z` and returns everything else unchanged.
    #[inline]
    pub fn to_ascii_lowercase(&self) -> Self {
        unsafe {
            match *self as u8 {
                b'A'...b'Z' => AsciiChar::from_unchecked(self.as_byte() + (b'a' - b'A')),
                _ => *self,
            }
        }
    }

    /// Compares two characters case-insensitively.
    pub fn eq_ignore_ascii_case(&self, other: &Self) -> bool {
        self.to_ascii_lowercase() == other.to_ascii_lowercase()
    }
}

impl fmt::Display for AsciiChar {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.as_char().fmt(f)
    }
}

impl fmt::Debug for AsciiChar {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.as_char().fmt(f)
    }
}

#[cfg(feature = "std")]
impl AsciiExt for AsciiChar {
    type Owned = AsciiChar;

    #[inline]
    fn is_ascii(&self) -> bool {
        true
    }

    #[inline]
    fn to_ascii_uppercase(&self) -> AsciiChar {
        unsafe {
            self.as_byte()
                .to_ascii_uppercase()
                .to_ascii_char_unchecked()
        }
    }

    #[inline]
    fn to_ascii_lowercase(&self) -> AsciiChar {
        unsafe {
            self.as_byte()
                .to_ascii_lowercase()
                .to_ascii_char_unchecked()
        }
    }

    fn eq_ignore_ascii_case(&self, other: &Self) -> bool {
        self.as_byte().eq_ignore_ascii_case(&other.as_byte())
    }

    #[inline]
    fn make_ascii_uppercase(&mut self) {
        *self = self.to_ascii_uppercase();
    }

    #[inline]
    fn make_ascii_lowercase(&mut self) {
        *self = self.to_ascii_lowercase();
    }
}

macro_rules! impl_into_partial_eq_ord {($wider:ty, $to_wider:expr) => {
    impl From<AsciiChar> for $wider {
        #[inline]
        fn from(a: AsciiChar) -> $wider {
            $to_wider(a)
        }
    }
    impl PartialEq<$wider> for AsciiChar {
        #[inline]
        fn eq(&self, rhs: &$wider) -> bool {
            $to_wider(*self) == *rhs
        }
    }
    impl PartialEq<AsciiChar> for $wider {
        #[inline]
        fn eq(&self, rhs: &AsciiChar) -> bool {
            *self == $to_wider(*rhs)
        }
    }
    impl PartialOrd<$wider> for AsciiChar {
        #[inline]
        fn partial_cmp(&self, rhs: &$wider) -> Option<Ordering> {
            $to_wider(*self).partial_cmp(rhs)
        }
    }
    impl PartialOrd<AsciiChar> for $wider {
        #[inline]
        fn partial_cmp(&self, rhs: &AsciiChar) -> Option<Ordering> {
            self.partial_cmp(&$to_wider(*rhs))
        }
    }
}}
impl_into_partial_eq_ord!{u8, AsciiChar::as_byte}
impl_into_partial_eq_ord!{char, AsciiChar::as_char}


/// Error returned by `ToAsciiChar`.
#[derive(PartialEq)]
pub struct ToAsciiCharError(());

const ERRORMSG_CHAR: &'static str = "not an ASCII character";

#[cfg(not(feature = "std"))]
impl ToAsciiCharError {
    /// Returns a description for this error, like `std::error::Error::description`.
    #[inline]
    pub fn description(&self) -> &'static str {
        ERRORMSG_CHAR
    }
}

impl fmt::Debug for ToAsciiCharError {
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        write!(fmtr, "{}", ERRORMSG_CHAR)
    }
}

impl fmt::Display for ToAsciiCharError {
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        write!(fmtr, "{}", ERRORMSG_CHAR)
    }
}

#[cfg(feature = "std")]
impl Error for ToAsciiCharError {
    #[inline]
    fn description(&self) -> &'static str {
        ERRORMSG_CHAR
    }
}

/// Convert `char`, `u8` and other character types to `AsciiChar`.
pub trait ToAsciiChar {
    /// Convert to `AsciiChar` without checking that it is an ASCII character.
    unsafe fn to_ascii_char_unchecked(self) -> AsciiChar;
    /// Convert to `AsciiChar`.
    fn to_ascii_char(self) -> Result<AsciiChar, ToAsciiCharError>;
}

impl ToAsciiChar for AsciiChar {
    #[inline]
    fn to_ascii_char(self) -> Result<AsciiChar, ToAsciiCharError> {
        Ok(self)
    }
    #[inline]
    unsafe fn to_ascii_char_unchecked(self) -> AsciiChar {
        self
    }
}

impl ToAsciiChar for u8 {
    #[inline]
    fn to_ascii_char(self) -> Result<AsciiChar, ToAsciiCharError> {
        unsafe {
            if self <= 0x7F {
                return Ok(self.to_ascii_char_unchecked());
            }
        }
        Err(ToAsciiCharError(()))
    }
    #[inline]
    unsafe fn to_ascii_char_unchecked(self) -> AsciiChar {
        mem::transmute(self)
    }
}

impl ToAsciiChar for char {
    #[inline]
    fn to_ascii_char(self) -> Result<AsciiChar, ToAsciiCharError> {
        unsafe {
            if self as u32 <= 0x7F {
                return Ok(self.to_ascii_char_unchecked());
            }
        }
        Err(ToAsciiCharError(()))
    }
    #[inline]
    unsafe fn to_ascii_char_unchecked(self) -> AsciiChar {
        (self as u8).to_ascii_char_unchecked()
    }
}

#[cfg(feature = "quickcheck")]
impl Arbitrary for AsciiChar {
    fn arbitrary<G: Gen>(g: &mut G) -> Self {
        let mode = g.gen_range(0, 100);
        match mode {
            0...14 => {
                // Control characters
                unsafe { AsciiChar::from_unchecked(g.gen_range(0, 0x1F) as u8) }
            }
            15...39 => {
                // Characters often used in programming languages
                *g.choose(&[
                    AsciiChar::Space, AsciiChar::Tab, AsciiChar::LineFeed, AsciiChar::Tilde,
                    AsciiChar::Grave, AsciiChar::Exclamation, AsciiChar::At, AsciiChar::Hash,
                    AsciiChar::Dollar, AsciiChar::Percent, AsciiChar::Ampersand,
                    AsciiChar::Asterisk, AsciiChar::ParenOpen, AsciiChar::ParenClose,
                    AsciiChar::UnderScore, AsciiChar::Minus, AsciiChar::Equal, AsciiChar::Plus,
                    AsciiChar::BracketOpen, AsciiChar::BracketClose, AsciiChar::CurlyBraceOpen,
                    AsciiChar::CurlyBraceClose, AsciiChar::Colon, AsciiChar::Semicolon,
                    AsciiChar::Apostrophe, AsciiChar::Quotation, AsciiChar::BackSlash,
                    AsciiChar::VerticalBar, AsciiChar::Caret, AsciiChar::Comma, AsciiChar::LessThan,
                    AsciiChar::GreaterThan, AsciiChar::Dot, AsciiChar::Slash, AsciiChar::Question,
                    AsciiChar::_0, AsciiChar::_1, AsciiChar::_2, AsciiChar::_3, AsciiChar::_3,
                    AsciiChar::_4 , AsciiChar::_6, AsciiChar::_7, AsciiChar::_8, AsciiChar::_9,
                ]).unwrap()
            }
            40...99 => {
                // Completely arbitrary characters
                unsafe { AsciiChar::from_unchecked(g.gen_range(0, 0x7F) as u8) }
            }
            _ => unreachable!(),
        }
    }

    fn shrink(&self) -> Box<Iterator<Item = Self>> {
        Box::new((*self as u8).shrink().filter_map(
            |x| AsciiChar::from(x).ok(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::{AsciiChar, ToAsciiChar, ToAsciiCharError};
    use AsciiChar::*;

    #[test]
    fn to_ascii_char() {
        fn generic<C: ToAsciiChar>(ch: C) -> Result<AsciiChar, ToAsciiCharError> {
            ch.to_ascii_char()
        }
        assert_eq!(generic(A), Ok(A));
        assert_eq!(generic(b'A'), Ok(A));
        assert_eq!(generic('A'), Ok(A));
        assert!(generic(200).is_err());
        assert!(generic('λ').is_err());
    }

    #[test]
    fn as_byte_and_char() {
        assert_eq!(A.as_byte(), b'A');
        assert_eq!(A.as_char(), 'A');
    }

    #[test]
    fn is_digit() {
        assert_eq!('0'.to_ascii_char().unwrap().is_digit(), true);
        assert_eq!('9'.to_ascii_char().unwrap().is_digit(), true);
        assert_eq!('/'.to_ascii_char().unwrap().is_digit(), false);
        assert_eq!(':'.to_ascii_char().unwrap().is_digit(), false);
    }

    #[test]
    fn is_control() {
        assert_eq!(US.is_control(), true);
        assert_eq!(DEL.is_control(), true);
        assert_eq!(Space.is_control(), false);
    }

    #[test]
    fn cmp_wider() {
        assert_eq!(A, 'A');
        assert_eq!(b'b', b);
        assert!(a < 'z');
    }

    #[test]
    fn ascii_case() {
        assert_eq!(At.to_ascii_lowercase(), At);
        assert_eq!(At.to_ascii_uppercase(), At);
        assert_eq!(A.to_ascii_lowercase(), a);
        assert_eq!(A.to_ascii_uppercase(), A);
        assert_eq!(a.to_ascii_lowercase(), a);
        assert_eq!(a.to_ascii_uppercase(), A);

        assert!(LineFeed.eq_ignore_ascii_case(&LineFeed));
        assert!(!LineFeed.eq_ignore_ascii_case(&CarriageReturn));
        assert!(z.eq_ignore_ascii_case(&Z));
        assert!(Z.eq_ignore_ascii_case(&z));
        assert!(!Z.eq_ignore_ascii_case(&DEL));
    }

    #[test]
    #[cfg(feature = "std")]
    fn ascii_ext() {
        #[allow(deprecated)]
        use std::ascii::AsciiExt;
        assert!(AsciiExt::is_ascii(&Null));
        assert!(AsciiExt::is_ascii(&DEL));
        assert!(AsciiExt::eq_ignore_ascii_case(&a, &A));
        assert!(!AsciiExt::eq_ignore_ascii_case(&A, &At));
        assert_eq!(AsciiExt::to_ascii_lowercase(&A), a);
        assert_eq!(AsciiExt::to_ascii_uppercase(&A), A);
        assert_eq!(AsciiExt::to_ascii_lowercase(&a), a);
        assert_eq!(AsciiExt::to_ascii_uppercase(&a), A);
        assert_eq!(AsciiExt::to_ascii_lowercase(&At), At);
        assert_eq!(AsciiExt::to_ascii_uppercase(&At), At);
        let mut mutable = (A,a);
        AsciiExt::make_ascii_lowercase(&mut mutable.0);
        AsciiExt::make_ascii_uppercase(&mut mutable.1);
        assert_eq!(mutable.0, a);
        assert_eq!(mutable.1, A);
    }

    #[test]
    #[cfg(feature = "std")]
    fn fmt_ascii() {
        assert_eq!(format!("{}", t), "t");
        assert_eq!(format!("{:?}", t), "'t'");
        assert_eq!(format!("{}", LineFeed), "\n");
        assert_eq!(format!("{:?}", LineFeed), "'\\n'");
    }
}
