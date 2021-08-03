#[macro_use]
extern crate nom;

use nom::{error::ErrorKind, AsBytes, FindSubstring, IResult, InputLength, Slice};
use nom_locate::LocatedSpan;
use std::cmp;
use std::fmt::Debug;
use std::ops::{Range, RangeFull};

type StrSpan<'a> = LocatedSpan<&'a str>;
type BytesSpan<'a> = LocatedSpan<&'a [u8]>;

#[cfg(any(feature = "std", feature = "alloc"))]
named!(simple_parser_str< StrSpan, Vec<StrSpan> >, do_parse!(
    foo: ws!(tag!("foo")) >>
    bar: ws!(tag!("bar")) >>
    baz: many0!(complete!(ws!(tag!("baz")))) >>
    eof: eof!() >>
    ({
        let mut res = vec![foo, bar];
        res.extend(baz);
        res.push(eof);
        res
    })
));

#[cfg(any(feature = "std", feature = "alloc"))]
named!(simple_parser_u8< BytesSpan, Vec<BytesSpan> >, do_parse!(
    foo: ws!(tag!("foo")) >>
    bar: ws!(tag!("bar")) >>
    baz: many0!(complete!(ws!(tag!("baz")))) >>
    eof: eof!() >>
    ({
        let mut res = vec![foo, bar];
        res.extend(baz);
        res.push(eof);
        res
    })
));

struct Position {
    line: u32,
    column: usize,
    offset: usize,
    fragment_len: usize,
}

fn test_str_fragments<'a, F, T>(parser: F, input: T, positions: Vec<Position>)
where
    F: Fn(LocatedSpan<T>) -> IResult<LocatedSpan<T>, Vec<LocatedSpan<T>>>,
    T: InputLength + Slice<Range<usize>> + Slice<RangeFull> + Debug + PartialEq + AsBytes,
{
    let res = parser(LocatedSpan::new(input.slice(..)))
        .map_err(|err| {
            eprintln!(
                "for={:?} -- The parser should run successfully\n{:?}",
                input, err
            );

            format!("The parser should run successfully")
        })
        .unwrap();
    // assert!(res.is_ok(), "the parser should run successfully");
    let (remaining, output) = res;
    assert!(
        remaining.fragment().input_len() == 0,
        "no input should remain"
    );
    assert_eq!(output.len(), positions.len());
    for (output_item, pos) in output.iter().zip(positions.iter()) {
        assert_eq!(output_item.location_offset(), pos.offset);
        assert_eq!(output_item.location_line(), pos.line);
        assert_eq!(
            output_item.fragment(),
            &input.slice(pos.offset..cmp::min(pos.offset + pos.fragment_len, input.input_len()))
        );
        assert_eq!(
            output_item.get_utf8_column(),
            pos.column,
            "columns should be equal"
        );
    }
}

#[cfg(any(feature = "std", feature = "alloc"))]
#[test]
fn it_locates_str_fragments() {
    test_str_fragments(
        simple_parser_str,
        "foobarbaz",
        vec![
            Position {
                line: 1,
                column: 1,
                offset: 0,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 4,
                offset: 3,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 7,
                offset: 6,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 10,
                offset: 9,
                fragment_len: 3,
            },
        ],
    );
    test_str_fragments(
        simple_parser_str,
        " foo
        bar
            baz",
        vec![
            Position {
                line: 1,
                column: 2,
                offset: 1,
                fragment_len: 3,
            },
            Position {
                line: 2,
                column: 9,
                offset: 13,
                fragment_len: 3,
            },
            Position {
                line: 3,
                column: 13,
                offset: 29,
                fragment_len: 3,
            },
            Position {
                line: 3,
                column: 16,
                offset: 32,
                fragment_len: 3,
            },
        ],
    );
}

#[cfg(any(feature = "std", feature = "alloc"))]
#[test]
fn it_locates_u8_fragments() {
    test_str_fragments(
        simple_parser_u8,
        b"foobarbaz",
        vec![
            Position {
                line: 1,
                column: 1,
                offset: 0,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 4,
                offset: 3,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 7,
                offset: 6,
                fragment_len: 3,
            },
            Position {
                line: 1,
                column: 10,
                offset: 9,
                fragment_len: 3,
            },
        ],
    );
    test_str_fragments(
        simple_parser_u8,
        b" foo
        bar
            baz",
        vec![
            Position {
                line: 1,
                column: 2,
                offset: 1,
                fragment_len: 3,
            },
            Position {
                line: 2,
                column: 9,
                offset: 13,
                fragment_len: 3,
            },
            Position {
                line: 3,
                column: 13,
                offset: 29,
                fragment_len: 3,
            },
            Position {
                line: 3,
                column: 16,
                offset: 32,
                fragment_len: 3,
            },
        ],
    );
}

fn find_substring<'a>(
    input: StrSpan<'a>,
    substr: &'static str,
) -> IResult<StrSpan<'a>, StrSpan<'a>> {
    let substr_len = substr.len();
    match input.find_substring(substr) {
        None => Err(nom::Err::Error(error_position!(input, ErrorKind::Tag))),
        Some(pos) => Ok((
            input.slice(pos + substr_len..),
            input.slice(pos..pos + substr_len),
        )),
    }
}

#[cfg(feature = "alloc")]
#[test]
fn test_escaped_string() {
    #[allow(unused)]
    use nom::Needed; // https://github.com/Geal/nom/issues/780
    named!(string<StrSpan, String>, delimited!(
        char!('"'),
        escaped_transform!(call!(nom::character::complete::alpha1), '\\', nom::character::complete::anychar),
        char!('"')
    ));

    let res = string(LocatedSpan::new("\"foo\\\"bar\""));
    assert!(res.is_ok());
    let (span, remaining) = res.unwrap();
    assert_eq!(span.location_offset(), 10);
    assert_eq!(span.location_line(), 1);
    assert_eq!(span.fragment(), &"");
    assert_eq!(remaining, "foo\"bar".to_string());
}

#[cfg(any(feature = "std", feature = "alloc"))]
named!(plague<StrSpan, Vec<StrSpan> >, do_parse!(
    ojczyzno: call!(find_substring, "Ojczyzno") >>
    jak: many0!(call!(find_substring, "jak ")) >>
    zielona: call!(find_substring, "Zielona") >>
    take_until!(".") >> tag!(".") >>
    ({
        let mut res = vec![ojczyzno];
        res.extend(jak);
        res.push(zielona);
        res
    })
));

#[cfg(any(feature = "std", feature = "alloc"))]
#[test]
fn it_locates_complex_fragments() {
    // Pan Tadeusz. https://pl.m.wikisource.org/wiki/Pan_Tadeusz_(wyd._1834)/Ksi%C4%99ga_pierwsza
    let input = "Litwo! Ojczyzno moja! ty jestes jak zdrowie;
Ile cie trzeba cenic, ten tylko sie dowie
Kto cie stracil. Dzis pieknosc twa w calej ozdobie
Widze i opisuje, bo tesknie po tobie.

Panno swieta, co jasnej bronisz Czestochowy
I w Ostrej swiecisz Bramie! Ty, co grod zamkowy

Nowogrodzki ochraniasz z jego wiernym ludem!
Jak mnie dziecko do zdrowia powrocilas cudem,
(Gdy od placzacej matki, pod Twoje opieke
Ofiarowany, martwa podnioslem powieke;
I zaraz moglem pieszo, do Twych swiatyn progu
Isc za wrocone zycie podziekowac Bogu;)
Tak nas powrocisz cudem na Ojczyzny lono.
Tymczasem przenos moje dusze uteskniona
Do tych pagorkow lesnych, do tych lak zielonych,
Szeroko nad blekitnym Niemnem rosciagnionych;
Do tych pol malowanych zbozem rozmaitem,
Wyzlacanych pszenica, posrebrzanych zytem;
Gdzie bursztynowy swierzop, gryka jak snieg biala,
Gdzie panienskim rumiencem dziecielina pala,
A wszystko przepasane jakby wstega, miedza
Zielona, na niej zrzadka ciche grusze siedza.";

    let expected = vec![
        Position {
            line: 1,
            column: 8,
            offset: 7,
            fragment_len: 8,
        },
        Position {
            line: 1,
            column: 33,
            offset: 32,
            fragment_len: 4,
        },
        Position {
            line: 21,
            column: 35,
            offset: 823,
            fragment_len: 4,
        },
        Position {
            line: 24,
            column: 1,
            offset: 928,
            fragment_len: 7,
        },
    ];

    test_str_fragments(plague, input, expected);
}
