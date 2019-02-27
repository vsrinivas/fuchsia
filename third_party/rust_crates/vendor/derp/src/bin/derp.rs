extern crate clap;
extern crate data_encoding;
extern crate derp;
extern crate pem;
extern crate untrusted;

use clap::{App, Arg, ArgMatches};
use derp::{Result, Error, Tag};
use std::fs::File;
use std::io::Read;
use untrusted::{Input, Reader};

fn main() {
    let matches = parser().get_matches();
    match run_main(matches) {
        Ok(s) => {
            println!("{}", s.trim());
            std::process::exit(0)
        }
        Err(e) => {
            println!("Error: {:?}", e);
            std::process::exit(1);
        }
    }
}

fn run_main(matches: ArgMatches) -> Result<String> {
    let mut file = File::open(matches.value_of("path").unwrap())?;
    let mut buf = Vec::new();
    file.read_to_end(&mut buf)?;
    let buf = parse_to_bytes(&buf);
    let input = Input::from(&buf);
    Ok(input.read_all(Error::Read, make_printable_string)?)
}

fn parser<'a, 'b>() -> App<'a, 'b> {
    App::new("derp")
        .version(env!("CARGO_PKG_VERSION"))
        .about("CLI tool for parsing and displaying DER")
        .arg(
            Arg::with_name("path")
                .takes_value(true)
                .required(true)
                .help("The path to the file")
        )
}

fn parse_to_bytes(bytes: &[u8]) -> Vec<u8> {
    data_encoding::BASE64.decode(bytes)
        .map_err(|_| ())
        .or_else(|_| {
            data_encoding::HEXUPPER.decode(bytes)
                .map_err(|_| ())
        })
        .or_else(|_| {
            data_encoding::HEXLOWER.decode(bytes)
                .map_err(|_| ())
        })
        .or_else(|_| {
            pem::parse(bytes)
                .map(|p| p.contents)
                .map_err(|_| ())
        })
        .unwrap_or_else(|_| bytes.to_vec())
}

fn make_printable_string<'a>(input: &mut Reader<'a>) -> Result<String> {
    if input.at_end() {
        return Ok("".into())
    }

    if input.peek(Tag::Sequence as u8) {
        let out = derp::nested(input, Tag::Sequence, make_printable_string)?
            .lines()
            .map(|l| format!("  {}\n", l))
            .collect::<String>();
        return Ok(format!("{}\n{}", Tag::Sequence, out))
    }

    if input.peek(Tag::BitString as u8) {
        let out = derp::nested(input, Tag::BitString, make_printable_string)
            .map(|s| {
                s.lines()
                    .map(|l| format!("  {}\n", l))
                    .collect::<String>()
            })
            .unwrap_or_else(|_| "".into());
        return Ok(format!("{}\n{}", Tag::BitString, out))
    }

    if input.peek(Tag::OctetString as u8) {
        let out = derp::nested(input, Tag::OctetString, make_printable_string)
            .map(|s| {
                s.lines()
                    .map(|l| format!("  {}\n", l))
                    .collect::<String>()
            })
            .unwrap_or_else(|_| "".into());
        return Ok(format!("{}\n{}", Tag::OctetString, out))
    }

    let mut out = String::new();
    while let Ok((tag, _)) = derp::read_tag_and_get_value(input) {
        out.push_str(&format!("{}\n", Tag::from_byte(tag).map(|t| format!("{}", t)).unwrap_or_else(|_| format!("0x{:x}", tag))))
    }
    Ok(out)
}

#[cfg(test)]
mod test {
    use super::*;

    const ED25519_PK8: &[u8] = include_bytes!("../../tests/ed25519.pk8");

    #[test]
    fn parse_ed25519_pk8() {
        let input = parse_to_bytes(ED25519_PK8);
        let input = Input::from(&input);
        input.read_all(Error::Read, make_printable_string).unwrap();
    }
}
