#![feature(test)]
extern crate test;

use nom::Slice;
use nom_locate::LocatedSpan;

use test::Bencher;

// Pan Tadeusz. https://pl.m.wikisource.org/wiki/Pan_Tadeusz_(wyd._1834)/Ksi%C4%99ga_pierwsza
const TEXT: &str = "Litwo! Ojczyzno moja! ty jesteś jak zdrowie;
Ile cię trzeba cenić, ten tylko się dowie
Kto cię stracił. Dziś piękność twą w całéj ozdobie
Widzę i opisuję, bo tęsknię po tobie.

Panno święta, co jasnéj bronisz Częstochowy
I w Ostréj świecisz Bramie! Ty, co gród zamkowy

Nowogródzki ochraniasz z jego wiernym ludem!
Jak mnie dziecko do zdrowia powróciłaś cudem,
(Gdy od płaczącéj matki, pod Twoję opiekę
Ofiarowany, martwą podniosłem powiekę;
I zaraz mogłem pieszo, do Twych świątyń progu
Iść za wrócone życie podziękować Bogu;)
Tak nas powrócisz cudem na Ojczyzny łono.
Tymczasem przenoś moję duszę utęsknioną
Do tych pagórków leśnych, do tych łąk zielonych,
Szeroko nad błękitnym Niemnem rosciągnionych;
Do tych pól malowanych zbożem rozmaitém,
Wyzłacanych pszenicą, posrebrzanych żytem;
Gdzie bursztynowy świerzop, gryka jak śnieg biała,
Gdzie panieńskim rumieńcem dzięcielina pała,
A wszystko przepasane jakby wstęgą, miedzą
Zieloną, na niéj zrzadka ciche grusze siedzą.

Sród takich pól przed laty, nad brzegiem ruczaju,
Na pagórku niewielkim, we brzozowym gaju,
Stał dwór szlachecki, z drzewa, lecz podmurowany;
Świéciły się zdaleka pobielane ściany,
Tém bielsze że odbite od ciemnéj zieleni

Topoli, co go bronią od wiatrów jesieni.
Dóm mieszkalny niewielki lecz zewsząd chędogi,
I stodołę miał wielką i przy niéj trzy stogi
Użątku, co pod strzechą zmieścić się niemoże;
Widać że okolica obfita we zboże,
I widać z liczby kopic, co wzdłuż i wszerz smugów
Świecą gęsto jak gwiazdy; widać z liczby pługów
Orzących wcześnie łany ogromne ugoru
Czarnoziemne, zapewne należne do dworu,
Uprawne dobrze nakształt ogrodowych grządek:
Że w tym domu dostatek mieszka i porządek.
Brama na wciąż otwarta przechodniom ogłasza,
Że gościnna, i wszystkich w gościnę zaprasza.";

const TEXT_ASCII: &str = "Litwo! Ojczyzno moja! ty jestes jak zdrowie;
Ile cie trzeba cenic, ten tylko sie dowie
Kto cie stracil. Dzis pieknosc twa w calej ozdobie
Widze i opisuje, bo tesknie po tobie.

Panno swieta, co jasnej bronisz Czestochowy
I w Ostrej swiecisz Bramie![1] Ty, co grod zamkowy

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
Zielona, na niej zrzadka ciche grusze siedza.

Srod takich pol przed laty, nad brzegiem ruczaju,
Na pagorku niewielkim, we brzozowym gaju,
Stal dwor szlachecki, z drzewa, lecz podmurowany;
Swiecily sie zdaleka pobielane sciany,
Tem bielsze ze odbite od ciemnej zieleni

Topoli, co go bronia od wiatrow jesieni.
Dom mieszkalny niewielki lecz zewszad chedogi,
I stodole mial wielka i przy niej trzy stogi
Uzatku, co pod strzecha zmiescic sie niemoze;
Widac ze okolica obfita we zboze,
I widac z liczby kopic, co wzdluz i wszerz smugow
Swieca gesto jak gwiazdy; widac z liczby plugow
Orzacych wczesnie lany ogromne ugoru
Czarnoziemne, zapewne nalezne do dworu,
Uprawne dobrze naksztalt ogrodowych grzadek:
Ze w tym domu dostatek mieszka i porzadek.
Brama na wciaz otwarta przechodniom oglasza,
Ze goscinna, i wszystkich w goscine zaprasza.";

#[bench]
fn bench_slice_full(b: &mut Bencher) {
    let input = LocatedSpan::new(TEXT);

    b.iter(|| {
        input.slice(..);
    });
}

#[bench]
fn bench_slice_from(b: &mut Bencher) {
    let input = LocatedSpan::new(TEXT);

    b.iter(|| {
        input.slice(200..);
    });
}

#[bench]
fn bench_slice_from_zero(b: &mut Bencher) {
    let input = LocatedSpan::new(TEXT);

    b.iter(|| {
        input.slice(0..);
    });
}

#[bench]
fn bench_slice_to(b: &mut Bencher) {
    let input = LocatedSpan::new(TEXT);

    b.iter(|| {
        input.slice(..200);
    });
}

#[bench]
fn bench_slice(b: &mut Bencher) {
    let input = LocatedSpan::new(TEXT);

    b.iter(|| {
        input.slice(200..300);
    });
}

#[bench]
fn bench_slice_columns_only(b: &mut Bencher) {
    let text = TEXT.replace("\n", "");
    let input = LocatedSpan::new(text.as_str());

    b.iter(|| {
        input.slice(499..501).get_utf8_column();
    });
}

#[bench]
fn bench_slice_columns_only_for_ascii_text(b: &mut Bencher) {
    #[allow(unused)]
    use std::ascii::AsciiExt;
    let text = TEXT_ASCII.replace("\n", "");
    let input = LocatedSpan::new(text.as_str());

    assert!(text.is_ascii());
    b.iter(|| {
        input.slice(500..501).get_column();
    });
}
