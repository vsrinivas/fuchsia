use iota::iota;

#[test]
fn test_iota() {
    iota! {
        const A: u8 = 1 << iota;
            , B
            , C
        const D: usize = 3;
        const E: i64 = iota * 2;
            , F
    }
    assert_eq!(A, 1);
    assert_eq!(B, 2);
    assert_eq!(C, 4);
    assert_eq!(D, 3);
    assert_eq!(E, 8);
    assert_eq!(F, 10);
}

#[test]
fn test_delimiters() {
    const S: [u8; 3] = [4, 5, 6];
    iota! {
        const A: u8 = S[iota];
        const B: (u8, u8) = (1 << iota, (1 << iota) - 1);
        const C: u8 = { const X: u8 = iota; X * 2 };
    }
    assert_eq!(A, 4);
    assert_eq!(B, (2, 1));
    assert_eq!(C, 4);
}
