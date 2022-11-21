#![feature(test)]

extern crate test;

#[bench]
fn bench_com(b: &mut test::Bencher) {
    let list = publicsuffix::List::fetch().unwrap();
    b.iter(|| {
        let res = list.parse_domain("raw.github.com").unwrap();
        assert_eq!(res.suffix().unwrap(), "com");
    });
}

#[bench]
fn bench_jp(b: &mut test::Bencher) {
    let list = publicsuffix::List::fetch().unwrap();
    b.iter(|| {
        let res = list.parse_domain("www.city.yamanashi.yamanashi.jp").unwrap();
        assert_eq!(res.suffix().unwrap(), "yamanashi.yamanashi.jp");
    });
}
