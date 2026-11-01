#ifndef LRP_SQUISH_H
#define LRP_SQUISH_H

/*
 * Squish message base, read the way GoldED does.
 *
 * .sql is an array of little-endian uint32 values, one per user. The
 * value is SqshIdx.msgno, the local message number, not the MSGID.
 * .sqi records are 12 bytes: frame offset, message number, to-hash.
 * The live length is num_msgs in the 256-byte area header, which is
 * how hpt bounds the index. Records are read from the file as needed;
 * the index and the message bodies are not loaded.
 * The frame in .sqd is a 28-byte header (id 0xAFAE4453) followed by a
 * 238-byte packed XMSG and then the control block. Kludges in that
 * block are separated by Ctrl-A (0x01) and have no CR of their own.
 */

#include "util.h"

#include <stdio.h>

#define SQ_FRAME_ID 0xAFAE4453u
#define SQ_FRAME_BYTES 28
#define SQ_XMSG_BYTES 238
#define SQ_IDX_BYTES 12

typedef struct Sq {
    FILE *sqd;
    FILE *sqi;
    char base[LRP_PATH];
    int user;
    int exclusive;
    /* Live index length, counted once. hpt trusts num_msgs in the
     * 256-byte area header; a header that does not say is scanned
     * until the first free record. The records themselves stay on
     * disk, so a large echo is not copied into memory. */
    int indexed;
    uint32_t nmsg;
} Sq;

/* 0 opened, 1 someone else holds .sqd, 2 the base files are not on
 * disk, -1 any other error. A missing base is not an error for the
 * caller: the echo was removed, and the rest of the sync continues. */
int sq_open(Sq *sq, const char *base, int user, int exclusive);
void sq_close(Sq *sq);

int sq_read_lastread(Sq *sq, uint32_t *msgno, int64_t *mtime);
int sq_write_lastread(Sq *sq, uint32_t msgno);

/* msgid_out receives at most 200 characters, matching GoldED msgids[201]. */
int sq_msgid_of(Sq *sq, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date);

typedef struct SqHit {
    int found;
    uint32_t msgno;
    int64_t date;
    int64_t oldest;
    int have_fallback;
    uint32_t fallback_msgno;
    char fallback_msgid[LRP_MSGID];
    int64_t fallback_date;
} SqHit;

int sq_find_msgid(Sq *sq, const char *msgid, int64_t target_date, SqHit *hit);

#endif
