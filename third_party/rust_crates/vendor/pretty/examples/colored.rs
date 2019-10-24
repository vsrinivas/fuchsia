extern crate pretty;

use pretty::{Arena, DocAllocator};
use pretty::termcolor::{Color, ColorChoice, ColorSpec, StandardStream};

fn main() {
    let arena = Arena::new();
    let red = arena
        .text("red")
        .annotate(ColorSpec::new().set_fg(Some(Color::Red)).clone());

    let blue = arena
        .text("blue")
        .annotate(ColorSpec::new().set_fg(Some(Color::Blue)).clone());

    let bold = arena
        .text("bold")
        .annotate(ColorSpec::new().set_bold(true).clone());

    let intense = arena
        .text("intense")
        .annotate(ColorSpec::new().set_intense(true).clone());

    red.append(arena.space())
        .append(blue)
        .append(arena.space())
        .append(bold)
        .append(arena.space())
        .append(intense)
        .group()
        .1
        .render_colored(80, StandardStream::stdout(ColorChoice::Auto))
        .unwrap();
}
