// Copyright 2017 The UNIC Project Developers.
//
// See the COPYRIGHT file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use unic_char_property::TotalCharProperty;

char_property! {
    /// Represents the Unicode Character
    /// [`General_Category`](http://unicode.org/reports/tr44/#General_Category) property.
    ///
    /// This is a useful breakdown into various character types which can be used as a default
    /// categorization in implementations. For the property values, see
    /// [`General_Category Values`](http://unicode.org/reports/tr44/#General_Category_Values).
    pub enum GeneralCategory {
        abbr => "gc";
        long => "General_Category";
        human => "General Category";

        /// An uppercase letter
        UppercaseLetter {
            abbr => Lu,
            long => Uppercase_Letter,
            human => "Uppercase Letter",
        }

        /// A lowercase letter
        LowercaseLetter {
            abbr => Ll,
            long => Lowercase_Letter,
            human => "Lowercase Letter",
        }

        /// A digraphic character, with first part uppercase
        TitlecaseLetter {
            abbr => Lt,
            long => Titlecase_Letter,
            human => "Titlecase Letter",
        }

        /// A modifier letter
        ModifierLetter {
            abbr => Lm,
            long => Modifier_Letter,
            human => "Modifier Letter",
        }

        /// Other letters, including syllables and ideographs
        OtherLetter {
            abbr => Lo,
            long => Other_Letter,
            human => "Other Letter",
        }

        /// A nonspacing combining mark (zero advance width)
        NonspacingMark {
            abbr => Mn,
            long => Nonspacing_Mark,
            human => "Nonspacing Mark",
        }

        /// A spacing combining mark (positive advance width)
        SpacingMark {
            abbr => Mc,
            long => Spacing_Mark,
            human => "Spacing Mark",
        }

        /// An enclosing combining mark
        EnclosingMark {
            abbr => Me,
            long => Enclosing_Mark,
            human => "Enclosing Mark",
        }

        /// A decimal digit
        DecimalNumber {
            abbr => Nd,
            long => Decimal_Number,
            human => "Decimal Digit",
        }

        /// A letterlike numeric character
        LetterNumber {
            abbr => Nl,
            long => Letter_Number,
            human => "Letterlike Number",
        }

        /// A numeric character of other type
        OtherNumber {
            abbr => No,
            long => Other_Number,
            human => "Other Numeric",
        }

        /// A connecting punctuation mark, like a tie
        ConnectorPunctuation {
            abbr => Pc,
            long => Connector_Punctuation,
            human => "Connecting Punctuation",
        }

        /// A dash or hyphen punctuation mark
        DashPunctuation {
            abbr => Pd,
            long => Dash_Punctuation,
            human => "Dash Punctuation",
        }

        /// An opening punctuation mark (of a pair)
        OpenPunctuation {
            abbr => Ps,
            long => Open_Punctuation,
            human => "Opening Punctuation",
        }

        /// A closing punctuation mark (of a pair)
        ClosePunctuation {
            abbr => Pe,
            long => Close_Punctuation,
            human => "Closing Punctuation",
        }

        /// An initial quotation mark
        InitialPunctuation {
            abbr => Pi,
            long => Initial_Punctuation,
            human => "Initial Quotation",
        }

        /// A final quotation mark
        FinalPunctuation {
            abbr => Pf,
            long => Final_Punctuation,
            human => "Final Quotation",
        }

        /// A punctuation mark of other type
        OtherPunctuation {
            abbr => Po,
            long => Other_Punctuation,
            human => "Other Punctuation",
        }

        /// A symbol of mathematical use
        MathSymbol {
            abbr => Sm,
            long => Math_Symbol,
            human => "Math Symbol",
        }

        /// A currency sign
        CurrencySymbol {
            abbr => Sc,
            long => Currency_Symbol,
            human => "Currency Symbol",
        }

        /// A non-letterlike modifier symbol
        ModifierSymbol {
            abbr => Sk,
            long => Modifier_Symbol,
            human => "Modifier Symbol",
        }

        /// A symbol of other type
        OtherSymbol {
            abbr => So,
            long => Other_Symbol,
            human => "Other Symbol",
        }

        /// A space character (of various non-zero widths)
        SpaceSeparator {
            abbr => Zs,
            long => Space_Separator,
            human => "Space",
        }

        /// U+2028 LINE SEPARATOR only
        LineSeparator {
            abbr => Zl,
            long => Line_Separator,
            human => "Line Separator",
        }

        /// U+2029 PARAGRAPH SEPARATOR only
        ParagraphSeparator {
            abbr => Zp,
            long => Paragraph_Separator,
            human => "Paragraph Separator",
        }

        /// A C0 or C1 control code
        Control {
            abbr => Cc,
            long => Control,
            human => "Control",
        }

        /// A format control character
        Format {
            abbr => Cf,
            long => Format,
            human => "Formatting",
        }

        /// A surrogate code point
        Surrogate {
            abbr => Cs,
            long => Surrogate,
            human => "Surrogate",
        }

        /// A private-use character
        PrivateUse {
            abbr => Co,
            long => Private_Use,
            human => "Private-Use",
        }

        /// Unassigned
        Unassigned {
            abbr => Cn,
            long => Unassigned,
            human => "Unassigned",
        }
    }

    pub mod abbr_names for abbr;
    pub mod long_names for long;
}

impl TotalCharProperty for GeneralCategory {
    fn of(ch: char) -> Self {
        Self::of(ch)
    }
}

impl Default for GeneralCategory {
    fn default() -> Self {
        GeneralCategory::Unassigned
    }
}

mod data {
    use super::abbr_names::*;
    use unic_char_property::tables::CharDataTable;
    pub const GENERAL_CATEGORY_TABLE: CharDataTable<super::GeneralCategory> =
        include!("../tables/general_category.rsv");
}

impl GeneralCategory {
    /// Find the `GeneralCategory` of a single char.
    pub fn of(ch: char) -> GeneralCategory {
        data::GENERAL_CATEGORY_TABLE.find_or_default(ch)
    }
}

impl GeneralCategory {
    /// `Lu` | `Ll` | `Lt`  (Short form: `LC`)
    pub fn is_cased_letter(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Lu | Ll | Lt)
    }

    /// `Lu` | `Ll` | `Lt` | `Lm` | `Lo`  (Short form: `L`)
    pub fn is_letter(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Lu | Ll | Lt | Lm | Lo)
    }

    /// `Mn` | `Mc` | `Me`  (Short form: `M`)
    pub fn is_mark(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Mn | Mc | Me)
    }

    /// `Nd` | `Nl` | `No`  (Short form: `N`)
    pub fn is_number(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Nd | Nl | No)
    }

    /// `Pc` | `Pd` | `Ps` | `Pe` | `Pi` | `Pf` | `Po`  (Short form: `P`)
    pub fn is_punctuation(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Pc | Pd | Ps | Pe | Pi | Pf | Po)
    }

    /// `Sm` | `Sc` | `Sk` | `So`  (Short form: `S`)
    pub fn is_symbol(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Sm | Sc | Sk | So)
    }

    /// `Zs` | `Zl` | `Zp`  (Short form: `Z`)
    pub fn is_separator(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Zs | Zl | Zp)
    }

    /// `Cc` | `Cf` | `Cs` | `Co` | `Cn`  (Short form: `C`)
    pub fn is_other(&self) -> bool {
        use self::abbr_names::*;
        matches!(*self, Cc | Cf | Cs | Co | Cn)
    }
}

#[cfg(test)]
mod tests {
    use super::GeneralCategory as GC;
    use core::char;
    use unic_char_property::EnumeratedCharProperty;

    #[test]
    fn test_ascii() {
        for c in 0x00..(0x1F + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::Control);
        }

        assert_eq!(GC::of(' '), GC::SpaceSeparator);
        assert_eq!(GC::of('!'), GC::OtherPunctuation);
        assert_eq!(GC::of('"'), GC::OtherPunctuation);
        assert_eq!(GC::of('#'), GC::OtherPunctuation);
        assert_eq!(GC::of('$'), GC::CurrencySymbol);
        assert_eq!(GC::of('%'), GC::OtherPunctuation);
        assert_eq!(GC::of('&'), GC::OtherPunctuation);
        assert_eq!(GC::of('\''), GC::OtherPunctuation);
        assert_eq!(GC::of('('), GC::OpenPunctuation);
        assert_eq!(GC::of(')'), GC::ClosePunctuation);
        assert_eq!(GC::of('*'), GC::OtherPunctuation);
        assert_eq!(GC::of('+'), GC::MathSymbol);
        assert_eq!(GC::of(','), GC::OtherPunctuation);
        assert_eq!(GC::of('-'), GC::DashPunctuation);
        assert_eq!(GC::of('.'), GC::OtherPunctuation);
        assert_eq!(GC::of('/'), GC::OtherPunctuation);

        for c in ('0' as u32)..('9' as u32 + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::DecimalNumber);
        }

        assert_eq!(GC::of(':'), GC::OtherPunctuation);
        assert_eq!(GC::of(';'), GC::OtherPunctuation);
        assert_eq!(GC::of('<'), GC::MathSymbol);
        assert_eq!(GC::of('='), GC::MathSymbol);
        assert_eq!(GC::of('>'), GC::MathSymbol);
        assert_eq!(GC::of('?'), GC::OtherPunctuation);
        assert_eq!(GC::of('@'), GC::OtherPunctuation);

        for c in ('A' as u32)..('Z' as u32 + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::UppercaseLetter);
        }

        assert_eq!(GC::of('['), GC::OpenPunctuation);
        assert_eq!(GC::of('\\'), GC::OtherPunctuation);
        assert_eq!(GC::of(']'), GC::ClosePunctuation);
        assert_eq!(GC::of('^'), GC::ModifierSymbol);
        assert_eq!(GC::of('_'), GC::ConnectorPunctuation);
        assert_eq!(GC::of('`'), GC::ModifierSymbol);

        for c in ('a' as u32)..('z' as u32 + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::LowercaseLetter);
        }

        assert_eq!(GC::of('{'), GC::OpenPunctuation);
        assert_eq!(GC::of('|'), GC::MathSymbol);
        assert_eq!(GC::of('}'), GC::ClosePunctuation);
        assert_eq!(GC::of('~'), GC::MathSymbol);
    }

    #[test]
    fn test_bmp_edge() {
        // 0xFEFF ZERO WIDTH NO-BREAK SPACE (or) BYTE ORDER MARK
        let bom = '\u{FEFF}';
        assert_eq!(GC::of(bom), GC::Format);
        // 0xFFFC OBJECT REPLACEMENT CHARACTER
        assert_eq!(GC::of('￼'), GC::OtherSymbol);
        // 0xFFFD REPLACEMENT CHARACTER
        assert_eq!(GC::of('�'), GC::OtherSymbol);

        for &c in [0xFFEF, 0xFFFE, 0xFFFF].iter() {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::Unassigned);
        }
    }

    #[test]
    fn test_private_use() {
        for c in 0xF_0000..(0xF_FFFD + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::PrivateUse);
        }

        for c in 0x10_0000..(0x10_FFFD + 1) {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::PrivateUse);
        }

        for &c in [0xF_FFFE, 0xF_FFFF, 0x10_FFFE, 0x10_FFFF].iter() {
            let c = char::from_u32(c).unwrap();
            assert_eq!(GC::of(c), GC::Unassigned);
        }
    }

    #[test]
    fn test_abbr_name() {
        assert_eq!(GC::UppercaseLetter.abbr_name(), "Lu");
        assert_eq!(GC::Unassigned.abbr_name(), "Cn");
    }

    #[test]
    fn test_long_name() {
        assert_eq!(GC::UppercaseLetter.long_name(), "Uppercase_Letter");
        assert_eq!(GC::Unassigned.long_name(), "Unassigned");
    }

    #[test]
    fn test_human_name() {
        assert_eq!(GC::UppercaseLetter.human_name(), "Uppercase Letter");
        assert_eq!(GC::Unassigned.human_name(), "Unassigned");
    }
}
