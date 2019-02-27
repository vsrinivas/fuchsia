#![allow(unknown_lints)]

use stb_truetype::FontInfo;
use std::borrow::Cow;

#[allow(clippy::cast_ptr_alignment)] // FIXME seems a bit dodgy
fn main() {
    let file = &include_bytes!("../fonts/Gudea-Regular.ttf")[..];
    let font = FontInfo::new(Cow::Borrowed(file), 0).unwrap();

    for info in font.get_font_name_strings() {
        let (name, pl_en_la, na) = info;
        let name = (match pl_en_la {
            Some(stb_truetype::PlatformEncodingLanguageId::Mac(
                Some(Ok(stb_truetype::MacEid::Roman)),
                _,
            )) => ::std::str::from_utf8(name).ok().map(Cow::Borrowed),
            Some(stb_truetype::PlatformEncodingLanguageId::Microsoft(
                Some(Ok(stb_truetype::MicrosoftEid::UnicodeBMP)),
                _,
            )) => {
                let name16be = unsafe {
                    ::std::slice::from_raw_parts(name.as_ptr() as *const u16, name.len() / 2)
                };
                let name16 = name16be
                    .iter()
                    .map(|&v| u16::from_be(v))
                    .collect::<Vec<_>>();
                String::from_utf16(&name16).ok().map(Cow::Owned)
            }
            Some(stb_truetype::PlatformEncodingLanguageId::Microsoft(
                Some(Ok(stb_truetype::MicrosoftEid::UnicodeFull)),
                _,
            )) => {
                let name16be = unsafe {
                    ::std::slice::from_raw_parts(name.as_ptr() as *const u16, name.len() / 2)
                };
                let name16 = name16be
                    .iter()
                    .map(|&v| u16::from_be(v))
                    .collect::<Vec<_>>();
                String::from_utf16(&name16).ok().map(Cow::Owned)
            }
            Some(_) => Some(Cow::Borrowed("(Unknown encoding)")),
            None => Some(Cow::Borrowed("(Unknown Platform ID)")),
        })
        .unwrap_or(Cow::Borrowed("(Encoding error)"));
        println!("{:?}, {:?}, {:?}", name, pl_en_la, na);
    }
}
