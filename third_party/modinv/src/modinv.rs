// Calculate the modular inverse of `num`, using Extended GCD.
//
// Reference:
// Brent & Zimmermann, Modern Computer Arithmetic, v0.5.9, Algorithm 1.20
pub fn inv_mod_u32(num: u32) -> u32 {
    // num needs to be relatively prime to 2**32 -- i.e. it must be odd.
    assert!(num % 2 != 0);

    let mut a: i64 = num as i64;
    let mut b: i64 = (u32::max_value() as i64) + 1;

    // ExtendedGcd
    // Input: positive integers a and b
    // Output: integers (g, u, v) such that g = gcd(a, b) = ua + vb
    // As we don't need v for modular inverse, we don't calculate it.

    // 1: (u, w) <- (1, 0)
    let mut u = 1;
    let mut w = 0;
    // 3: while b != 0
    while b != 0 {
        // 4: (q, r) <- DivRem(a, b)
        let q = a / b;
        let r = a % b;
        // 5: (a, b) <- (b, r)
        a = b;
        b = r;
        // 6: (u, w) <- (w, u - qw)
        let m = u - w * q;
        u = w;
        w = m;
    }

    assert!(a == 1);
    // Downcasting acts like a mod 2^32 too.
    u as u32
}
