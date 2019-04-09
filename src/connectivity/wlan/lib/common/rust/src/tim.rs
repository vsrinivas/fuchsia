use crate::ie::TimHeader;

pub fn is_traffic_buffered(header: &TimHeader, bitmap: &[u8], aid: usize) -> bool {
    let n1 = header.bmp_ctrl.offset() as usize * 2;
    let octet = aid / 8;

    let carries_aid = n1 <= octet && octet < bitmap.len() + n1;
    carries_aid && bitmap[octet - n1] & (1 << (aid % 8)) != 0
}

#[cfg(test)]
mod tests {
    use {super::*, crate::ie::BitmapControl};

    #[test]
    fn zero_offset() {
        let header = &TimHeader { dtim_period: 0, dtim_count: 0, bmp_ctrl: BitmapControl(0) };
        let bitmap = &[0b0010010][..];

        assert!(!is_traffic_buffered(header, bitmap, 0));
        assert!(is_traffic_buffered(header, bitmap, 1));
        assert!(!is_traffic_buffered(header, bitmap, 2));
        assert!(!is_traffic_buffered(header, bitmap, 3));
        assert!(is_traffic_buffered(header, bitmap, 4));
        assert!(!is_traffic_buffered(header, bitmap, 5));
        assert!(!is_traffic_buffered(header, bitmap, 6));
        assert!(!is_traffic_buffered(header, bitmap, 7));
        assert!(!is_traffic_buffered(header, bitmap, 100));
    }

    #[test]
    fn with_offset() {
        let mut bmp_ctrl = BitmapControl(0);
        bmp_ctrl.set_offset(1);
        let header = &TimHeader { dtim_period: 0, dtim_count: 0, bmp_ctrl };
        let bitmap = &[0b0010010][..];

        // Offset of 1 means "skip 16 bits"
        assert!(!is_traffic_buffered(header, bitmap, 15));
        assert!(!is_traffic_buffered(header, bitmap, 16));
        assert!(is_traffic_buffered(header, bitmap, 17));
        assert!(!is_traffic_buffered(header, bitmap, 18));
        assert!(!is_traffic_buffered(header, bitmap, 19));
        assert!(is_traffic_buffered(header, bitmap, 20));
        assert!(!is_traffic_buffered(header, bitmap, 21));
        assert!(!is_traffic_buffered(header, bitmap, 22));
        assert!(!is_traffic_buffered(header, bitmap, 100));
    }
}
