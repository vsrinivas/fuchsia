mod lib {
    #[cfg(feature = "std")]
    pub mod std {
        pub use std::string::ToString;
        pub use std::vec::Vec;
    }
    #[cfg(all(not(feature = "std"), feature = "alloc"))]
    pub mod std {
        pub use alloc::string::ToString;
        pub use alloc::vec::Vec;
    }
}

#[cfg(feature = "alloc")]
use lib::std::*;

use super::LocatedSpan;
#[cfg(feature = "alloc")]
use nom::ParseTo;
use nom::{
    error::ErrorKind, Compare, CompareResult, FindSubstring, FindToken, InputIter, InputTake,
    InputTakeAtPosition, Offset, Slice,
};

type StrSpan<'a> = LocatedSpan<&'a str>;
type BytesSpan<'a> = LocatedSpan<&'a [u8]>;
type StrSpanEx<'a, 'b> = LocatedSpan<&'a str, &'b str>;
type BytesSpanEx<'a, 'b> = LocatedSpan<&'a [u8], &'b str>;

#[test]
fn new_sould_be_the_same_as_new_extra() {
    let byteinput = &b"foobar"[..];
    assert_eq!(
        BytesSpan::new(byteinput),
        LocatedSpan::new_extra(byteinput, ())
    );
    let strinput = "foobar";
    assert_eq!(StrSpan::new(strinput), LocatedSpan::new_extra(strinput, ()));
}

#[test]
fn it_should_call_new_for_u8_successfully() {
    let input = &b"foobar"[..];
    let output = BytesSpan {
        offset: 0,
        line: 1,
        fragment: input,
        extra: (),
    };

    assert_eq!(BytesSpan::new(input), output);
}

#[test]
fn it_should_convert_from_u8_successfully() {
    let input = &b"foobar"[..];
    assert_eq!(BytesSpan::new(input), input.into());
    assert_eq!(BytesSpanEx::new_extra(input, "extra"), input.into());
}

#[test]
fn it_should_call_new_for_str_successfully() {
    let input = &"foobar"[..];
    let output = StrSpan {
        offset: 0,
        line: 1,
        fragment: input,
        extra: (),
    };

    assert_eq!(StrSpan::new(input), output);
}

#[test]
fn it_should_convert_from_str_successfully() {
    let input = &"foobar"[..];
    assert_eq!(StrSpan::new(input), input.into());
    assert_eq!(StrSpanEx::new_extra(input, "extra"), input.into());
}

#[test]
fn it_should_ignore_extra_for_equality() {
    let input = &"foobar"[..];

    assert_eq!(
        StrSpanEx::new_extra(input, "foo"),
        StrSpanEx::new_extra(input, "bar")
    );
}

#[test]
fn it_should_slice_for_str() {
    let str_slice = StrSpanEx::new_extra("foobar", "extra");
    assert_eq!(
        str_slice.slice(1..),
        StrSpanEx {
            offset: 1,
            line: 1,
            fragment: "oobar",
            extra: "extra",
        }
    );
    assert_eq!(
        str_slice.slice(1..3),
        StrSpanEx {
            offset: 1,
            line: 1,
            fragment: "oo",
            extra: "extra",
        }
    );
    assert_eq!(
        str_slice.slice(..3),
        StrSpanEx {
            offset: 0,
            line: 1,
            fragment: "foo",
            extra: "extra",
        }
    );
    assert_eq!(str_slice.slice(..), str_slice);
}

#[test]
fn it_should_slice_for_u8() {
    let bytes_slice = BytesSpanEx::new_extra(b"foobar", "extra");
    assert_eq!(
        bytes_slice.slice(1..),
        BytesSpanEx {
            offset: 1,
            line: 1,
            fragment: b"oobar",
            extra: "extra",
        }
    );
    assert_eq!(
        bytes_slice.slice(1..3),
        BytesSpanEx {
            offset: 1,
            line: 1,
            fragment: b"oo",
            extra: "extra",
        }
    );
    assert_eq!(
        bytes_slice.slice(..3),
        BytesSpanEx {
            offset: 0,
            line: 1,
            fragment: b"foo",
            extra: "extra",
        }
    );
    assert_eq!(bytes_slice.slice(..), bytes_slice);
}

#[test]
fn it_should_calculate_columns() {
    let input = StrSpan::new(
        "foo
        bar",
    );

    let bar_idx = input.find_substring("bar").unwrap();
    assert_eq!(input.slice(bar_idx..).get_column(), 9);
}

#[test]
fn it_should_calculate_columns_accurately_with_non_ascii_chars() {
    let s = StrSpan::new("メカジキ");
    assert_eq!(s.slice(6..).get_utf8_column(), 3);
}

#[test]
#[should_panic(expected = "offset is too big")]
fn it_should_panic_when_getting_column_if_offset_is_too_big() {
    let s = StrSpanEx {
        offset: usize::max_value(),
        fragment: "",
        line: 1,
        extra: "",
    };
    s.get_column();
}

#[cfg(feature = "alloc")]
#[test]
fn it_should_iterate_indices() {
    let str_slice = StrSpan::new("foobar");
    assert_eq!(
        str_slice.iter_indices().collect::<Vec<(usize, char)>>(),
        vec![(0, 'f'), (1, 'o'), (2, 'o'), (3, 'b'), (4, 'a'), (5, 'r')]
    );
    assert_eq!(
        StrSpan::new("")
            .iter_indices()
            .collect::<Vec<(usize, char)>>(),
        vec![]
    );
}

#[cfg(feature = "alloc")]
#[test]
fn it_should_iterate_elements() {
    let str_slice = StrSpan::new("foobar");
    assert_eq!(
        str_slice.iter_elements().collect::<Vec<char>>(),
        vec!['f', 'o', 'o', 'b', 'a', 'r']
    );
    assert_eq!(
        StrSpan::new("").iter_elements().collect::<Vec<char>>(),
        vec![]
    );
}

#[test]
fn it_should_position_char() {
    let str_slice = StrSpan::new("foobar");
    assert_eq!(str_slice.position(|x| x == 'a'), Some(4));
    assert_eq!(str_slice.position(|x| x == 'c'), None);
}

#[test]
fn it_should_compare_elements() {
    assert_eq!(StrSpan::new("foobar").compare("foo"), CompareResult::Ok);
    assert_eq!(StrSpan::new("foobar").compare("bar"), CompareResult::Error);
    assert_eq!(StrSpan::new("foobar").compare("foobar"), CompareResult::Ok);
    assert_eq!(
        StrSpan::new("foobar").compare_no_case("fooBar"),
        CompareResult::Ok
    );
    assert_eq!(
        StrSpan::new("foobar").compare("foobarbaz"),
        CompareResult::Incomplete
    );
    assert_eq!(
        BytesSpan::new(b"foobar").compare(b"foo" as &[u8]),
        CompareResult::Ok
    );
}

#[test]
#[allow(unused_parens)]
fn it_should_find_token() {
    assert!(StrSpan::new("foobar").find_token('a'));
    assert!(StrSpan::new("foobar").find_token(b'a'));
    assert!(StrSpan::new("foobar").find_token(&(b'a')));
    assert!(!StrSpan::new("foobar").find_token('c'));
    assert!(!StrSpan::new("foobar").find_token(b'c'));
    assert!(!StrSpan::new("foobar").find_token((&b'c')));

    assert!(BytesSpan::new(b"foobar").find_token(b'a'));
    assert!(BytesSpan::new(b"foobar").find_token(&(b'a')));
    assert!(!BytesSpan::new(b"foobar").find_token(b'c'));
    assert!(!BytesSpan::new(b"foobar").find_token((&b'c')));
}

#[test]
fn it_should_find_substring() {
    assert_eq!(StrSpan::new("foobar").find_substring("bar"), Some(3));
    assert_eq!(StrSpan::new("foobar").find_substring("baz"), None);
    assert_eq!(BytesSpan::new(b"foobar").find_substring("bar"), Some(3));
    assert_eq!(BytesSpan::new(b"foobar").find_substring("baz"), None);
}

#[cfg(feature = "alloc")]
#[test]
fn it_should_parse_to_string() {
    assert_eq!(
        StrSpan::new("foobar").parse_to(),
        Some("foobar".to_string())
    );
    assert_eq!(
        BytesSpan::new(b"foobar").parse_to(),
        Some("foobar".to_string())
    );
}

// https://github.com/Geal/nom/blob/eee82832fafdfdd0505546d224caa466f7d39a15/src/util.rs#L710-L720
#[test]
fn it_should_calculate_offset_for_u8() {
    let s = b"abcd123";
    let a = &s[..];
    let b = &a[2..];
    let c = &a[..4];
    let d = &a[3..5];
    assert_eq!(a.offset(b), 2);
    assert_eq!(a.offset(c), 0);
    assert_eq!(a.offset(d), 3);
}

// https://github.com/Geal/nom/blob/eee82832fafdfdd0505546d224caa466f7d39a15/src/util.rs#L722-L732
#[test]
fn it_should_calculate_offset_for_str() {
    let s = StrSpan::new("abcřèÂßÇd123");
    let a = s.slice(..);
    let b = a.slice(7..);
    let c = a.slice(..5);
    let d = a.slice(5..9);
    assert_eq!(a.offset(&b), 7);
    assert_eq!(a.offset(&c), 0);
    assert_eq!(a.offset(&d), 5);
}

#[test]
fn it_should_take_chars() {
    let s = StrSpanEx::new_extra("abcdefghij", "extra");
    assert_eq!(
        s.take(5),
        StrSpanEx {
            offset: 0,
            line: 1,
            fragment: "abcde",
            extra: "extra",
        }
    );
}

#[test]
fn it_should_take_split_chars() {
    let s = StrSpanEx::new_extra("abcdefghij", "extra");
    assert_eq!(
        s.take_split(5),
        (
            StrSpanEx {
                offset: 5,
                line: 1,
                fragment: "fghij",
                extra: "extra",
            },
            StrSpanEx {
                offset: 0,
                line: 1,
                fragment: "abcde",
                extra: "extra",
            }
        )
    );
}

type TestError<'a, 'b> = (LocatedSpan<&'a str, &'b str>, nom::error::ErrorKind);

#[test]
fn it_should_split_at_position() {
    let s = StrSpanEx::new_extra("abcdefghij", "extra");
    assert_eq!(
        s.split_at_position::<_, TestError>(|c| { c == 'f' }),
        Ok((
            StrSpanEx {
                offset: 5,
                line: 1,
                fragment: "fghij",
                extra: "extra",
            },
            StrSpanEx {
                offset: 0,
                line: 1,
                fragment: "abcde",
                extra: "extra",
            }
        ))
    );
}

// TODO also test split_at_position with an error

#[test]
fn it_should_split_at_position1() {
    let s = StrSpanEx::new_extra("abcdefghij", "extra");
    assert_eq!(
        s.split_at_position1::<_, TestError>(|c| { c == 'f' }, ErrorKind::Alpha),
        s.split_at_position::<_, TestError>(|c| { c == 'f' }),
    );
}

#[test]
fn it_should_capture_position() {
    use super::position;
    use nom::bytes::complete::{tag, take_until};
    use nom::IResult;

    fn parser<'a>(s: StrSpan<'a>) -> IResult<StrSpan<'a>, (StrSpan<'a>, &'a str)> {
        let (s, _) = take_until("def")(s)?;
        let (s, p) = position(s)?;
        let (s, t) = tag("def")(s)?;
        Ok((s, (p, t.fragment)))
    }

    let s = StrSpan::new("abc\ndefghij");
    let (_, (s, t)) = parser(s).unwrap();
    assert_eq!(s.offset, 4);
    assert_eq!(s.line, 2);
    assert_eq!(t, "def");
}

#[test]
fn it_should_deref_to_fragment() {
    let input = &"foobar"[..];
    assert_eq!(*StrSpanEx::new_extra(input, "extra"), input);
    let input = &b"foobar"[..];
    assert_eq!(*BytesSpanEx::new_extra(input, "extra"), input);
}

#[cfg(feature = "std")]
#[test]
fn it_should_display_hex() {
    use nom::HexDisplay;
    assert_eq!(
        StrSpan::new(&"abc"[..]).to_hex(4),
        "00000000\t61 62 63    \tabc\n".to_owned()
    );
    assert_eq!(
        BytesSpanEx::new_extra(&b"abc"[..], "extra").to_hex(4),
        "00000000\t61 62 63    \tabc\n".to_owned()
    );
}

#[test]
fn line_of_empty_span_is_empty() {
    assert_eq!(StrSpan::new("").get_line_beginning(), "".as_bytes());
}

#[test]
fn line_of_single_line_start_is_whole() {
    assert_eq!(
        StrSpan::new("A single line").get_line_beginning(),
        "A single line".as_bytes(),
    );
}
#[test]
fn line_of_single_line_end_is_whole() {
    let data = "A single line";
    assert_eq!(
        StrSpan::new(data).slice(data.len()..).get_line_beginning(),
        "A single line".as_bytes(),
    );
}

#[test]
fn line_of_start_is_first() {
    assert_eq!(
        StrSpan::new(
            "One line of text\
             \nFollowed by a second\
             \nand a third\n"
        )
        .get_line_beginning(),
        "One line of text".as_bytes(),
    );
}

#[test]
fn line_of_nl_is_before() {
    let data = "One line of text\
         \nFollowed by a second\
         \nand a third\n";
    assert_eq!(
        StrSpan::new(data)
            .slice(data.find('\n').unwrap()..)
            .get_line_beginning(),
        "One line of text".as_bytes(),
    );
}

#[test]
fn line_of_end_after_nl_is_empty() {
    let data = "One line of text\
         \nFollowed by a second\
         \nand a third\n";
    assert_eq!(
        StrSpan::new(data).slice(data.len()..).get_line_beginning(),
        "".as_bytes(),
    );
}

#[test]
fn line_of_end_no_nl_is_last() {
    let data = "One line of text\
         \nFollowed by a second\
         \nand a third";
    assert_eq!(
        StrSpan::new(data).slice(data.len()..).get_line_beginning(),
        "and a third".as_bytes(),
    );
}

/// This test documents how `get_line_beginning()` differs from
/// a hypotetical `get_line()` method.
#[test]
fn line_begining_may_ot_be_entire_len() {
    let data = "One line of text\
         \nFollowed by a second\
         \nand a third";
    let by = "by";
    let pos = data.find_substring(by).unwrap();
    assert_eq!(
        StrSpan::new(data).slice(pos..pos+by.len()).get_line_beginning(),
        "Followed by".as_bytes(),
    );
}

#[cfg(feature = "std")]
#[test]
fn line_for_non_ascii_chars() {
    let data = StrSpan::new(
        "Några rader text på Svenska.\
         \nFörra raden var först, den här är i mitten\
         \noch här är sista raden.\n",
    );
    let s = data.slice(data.find_substring("först").unwrap()..);
    assert_eq!(
        format!(
            "{line_no:3}: {line_text}\n    {0:>lpos$}^- The match\n",
            "",
            line_no = s.location_line(),
            line_text = core::str::from_utf8(s.get_line_beginning()).unwrap(),
            lpos = s.get_utf8_column(),
        ),
        "  2: Förra raden var först, den här är i mitten\
       \n                     ^- The match\n",
    );
}
