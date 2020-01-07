use std::ops::*;

use super::MaybeOwned;

macro_rules! impl_op {
    ($([$OP:ident : $op:ident, $OP_ASSIGN:ident : $op_assign: ident]),*) => ($(
        impl<'min, L, R, OUT: 'min> $OP<MaybeOwned<'min, R>> for MaybeOwned<'min, L>
            where L: $OP<R, Output=OUT> + $OP<&'min R, Output=OUT>,
                &'min L: $OP<R, Output=OUT> + $OP<&'min R, Output=OUT>
        {
            type Output = OUT;

            fn $op(self, rhs: MaybeOwned<'min, R>) -> Self::Output {
                use self::MaybeOwned::*;
                match (self, rhs) {
                    (Owned(l), Owned(r)) => l.$op(r),
                    (Owned(l), Borrowed(r)) => l.$op(r),
                    (Borrowed(l), Owned(r)) => l.$op(r),
                    (Borrowed(l), Borrowed(r)) => l.$op(r)
                }
            }
        }

        impl<'min, L, R> $OP_ASSIGN<MaybeOwned<'min, R>> for MaybeOwned<'min, L>
            where L: Clone + $OP_ASSIGN<R> + $OP_ASSIGN<&'min R>
        {

            fn $op_assign(&mut self, rhs: MaybeOwned<'min, R>) {
                use self::MaybeOwned::*;
                match rhs {
                    Owned(r) => self.to_mut().$op_assign(r),
                    Borrowed(r) => self.to_mut().$op_assign(r)
                }
            }
        }
    )*);
}

impl_op! {
    [Add: add, AddAssign: add_assign],
    [Sub: sub, SubAssign: sub_assign],
    [Mul: mul, MulAssign: mul_assign],
    [Div: div, DivAssign: div_assign],
    [Shl: shl, ShlAssign: shl_assign],
    [Shr: shr, ShrAssign: shr_assign],
    [BitAnd: bitand, BitAndAssign: bitand_assign],
    [BitOr:  bitor,  BitOrAssign:  bitor_assign ],
    [BitXor: bitxor, BitXorAssign: bitxor_assign]
}

impl<'l, V, OUT> Neg for MaybeOwned<'l, V>
    where V: Neg<Output=OUT>, &'l V: Neg<Output=OUT>
{
    type Output = OUT;

    fn neg(self) -> Self::Output {
        use self::MaybeOwned::*;

        match self {
            Owned(s) => s.neg(),
            Borrowed(s) => s.neg()
        }
    }
}

impl<'l, V, OUT> Not for MaybeOwned<'l, V>
    where V: Not<Output=OUT>, &'l V: Not<Output=OUT>
{
    type Output = V::Output;

    fn not(self) -> Self::Output {
        use self::MaybeOwned::*;

        match self {
            Owned(s) => s.not(),
            Borrowed(s) => s.not()
        }
    }
}

#[cfg(test)]
mod test {
    use std::ops::{Add, AddAssign, Not, Neg};
    use super::*;

    #[derive(Clone, PartialEq)]
    struct Think { x: u8 }

    impl Add<Think> for Think {
        type Output = u8;

        fn add(self, rhs: Think) -> Self::Output {
            self.x + rhs.x
        }
    }
    impl AddAssign<Think> for Think {
        fn add_assign(&mut self, rhs: Think) {
            self.x += rhs.x
        }
    }
    impl<'a> Add<&'a Think> for Think {
        type Output = u8;

        fn add(self, rhs: &'a Think) -> Self::Output {
            self.x + rhs.x
        }
    }
    impl<'a> AddAssign<&'a Think> for Think {
        fn add_assign(&mut self, rhs: &'a Think) {
            self.x += rhs.x
        }
    }
    impl<'a> Add<Think> for &'a Think {
        type Output = u8;

        fn add(self, rhs: Think) -> Self::Output {
            self.x + rhs.x
        }
    }
    impl<'a, 'b> Add<&'a Think> for &'b Think {
        type Output = u8;

        fn add(self, rhs: &'a Think) -> Self::Output {
            self.x + rhs.x
        }
    }

    impl Not for Think {
        type Output = bool;

        fn not(self) -> Self::Output {
            self.x != 0
        }
    }

    impl<'a> Not for &'a Think {
        type Output = bool;

        fn not(self) -> Self::Output {
            self.x != 0
        }
    }

    impl Neg for Think {
        type Output = i8;

        fn neg(self) -> Self::Output {
            -(self.x as i8)
        }
    }

    impl<'a> Neg for &'a Think {
        type Output = i8;

        fn neg(self) -> Self::Output {
            -(self.x as i8)
        }
    }


    #[test]
    fn op_impls_exist() {
        let a = MaybeOwned::from(Think { x: 12 });
        let b = MaybeOwned::from(Think { x: 13 });
        assert_eq!(a + b, 25u8);

        let c = Think { x: 42 };
        let c1: MaybeOwned<Think> = (&c).into();
        let c2: MaybeOwned<Think> = (&c).into();

        assert_eq!(c1 + c2, 84);
    }

    #[test]
    fn op_assign_impls_exist() {
        let mut a = MaybeOwned::from(Think { x: 2 });
        a += MaybeOwned::from(Think { x: 3 });
        assert_eq!(a.x, 5);

        let a = Think { x: 2 };
        let mut a: MaybeOwned<Think> = (&a).into();
        assert!(!a.is_owned());
        a += MaybeOwned::from(Think { x: 5 });
        assert!(a.is_owned());
        assert_eq!(a.as_ref().x, 7);
    }

    #[test]
    fn not_and_neg_are_impl() {
        let a = Think { x: 5 };
        let b = Think { x: 0 };
        let a1: MaybeOwned<Think> = (&a).into();
        let a2: MaybeOwned<Think> = (&a).into();

        assert_eq!(!a1, true);
        assert_eq!(!b, false);
        assert_eq!(-a2, -5i8);
    }

}