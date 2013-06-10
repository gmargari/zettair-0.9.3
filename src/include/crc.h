/* crc.h declares a module that calculates cyclic redundancy checksums over
 * arbitrary sequences of bytes.
 *
 * written nml 2004-08-26
 *
 */

#ifndef CRC_H
#define CRC_H

#include <zstdint.h>

struct crc;

/* create a new CRC object */
struct crc *crc_new();

/* reinitialise the CRC object, to restart the checksumming */
void crc_reinit(struct crc *crc);

/* add avail_in bytes, from next_in to next_in + avail_in, to the checksum */
void crc(struct crc *crc, const void *next_in, unsigned int avail_in);

/* retrieve the current checksum */
uint32_t crc_sum(struct crc *crc);

/* delete a CRC object */
void crc_delete(struct crc *crc);

#endif

