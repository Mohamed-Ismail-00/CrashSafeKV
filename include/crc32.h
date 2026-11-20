/*
 * crc32.h - CRC32 (IEEE 802.3, reflected polynomial 0xEDB88320).
 * Same checksum as zlib / PNG / Ethernet: crc32("123456789") == 0xCBF43926.
 */
#ifndef CSKV_CRC32_H
#define CSKV_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Checksum of one buffer. */
uint32_t crc32_compute(const void *data, size_t len);

/* Incremental form: start with crc = 0 and feed chunks in order.
 * crc32_update(crc32_update(0, a, na), b, nb) == crc32_compute(a||b). */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#endif /* CSKV_CRC32_H */
