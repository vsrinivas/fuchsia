#ifndef __CKSUM_H
#define __CKSUM_H

#include <stddef.h>
#include <stdint.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS

/*
 * Computes the CRC-CCITT, starting with an initialization value.
 * buf: the data on which to apply the checksum
 * length: the number of bytes of data in 'buf' to be calculated.
 */
uint16_t crc16(const uint8_t *buf, size_t length);

/*
 * Computes an updated version of the CRC-CCITT from existing CRC.
 * crc: the previous values of the CRC
 * buf: the data on which to apply the checksum
 * length: the number of bytes of data in 'buf' to be calculated.
 */
uint16_t update_crc16(uint16_t crc, const uint8_t *buf, size_t len);

uint32_t crc32(uint32_t crc, const uint8_t *buf, size_t len);

uint32_t crc32_combine(uint32_t crc1, uint32_t crc2, size_t len2);

uint32_t adler32(uint32_t adler, const uint8_t *buf, size_t len);

uint32_t adler32_combine(uint32_t adler1, uint32_t adler2, size_t len2);

const uint32_t* get_crc_table(void);

__END_CDECLS

#endif

