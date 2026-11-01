#ifndef LRP_JAM_H
#define LRP_JAM_H

/*
 * JAM message base, read the way hpt and GoldED do.
 *
 * Four files share one stem: .jhr (headers), .jdt (text), .jdx (index),
 * .jlr (lastread). The text file is never opened. A message number is
 * BaseMsgNum plus its index slot, and BaseMsgNum of 0 means 1. A slot
 * whose index words are both 0xffffffff has been deleted. MSGID is
 * subfield 4 in the header, not a line in the body.
 *
 * Lastread is one 16-byte record per user: CRC of the name, user id,
 * last message read, highest message read. Only the last-read number
 * is updated. HighReadMsg is GoldED's high water and is left alone.
 * The name CRC is the JAM CRC-32 (polynomial 0xedb88320, seed
 * 0xffffffff, final complement). GoldED stores the running CRC
 * without that complement and looks a user up by that value, so a
 * record matches either form and a record we create uses GoldED's.
 * Letters are lowercased A-Z only, as the JAM spec requires. The
 * index and the headers are read from disk as needed;
 * a large area is not copied into memory.
 */

#include "squish.h"

typedef struct Jam {
    FILE *jhr;
    FILE *jdx;
    char base[LRP_PATH];
    char user[80];
    int prepared;
    uint32_t base_msg;
    uint32_t nidx;
    /* Byte offset of our .jlr record, or -1 when this user has none. */
    int64_t lr_at;
    uint32_t lr_high;
    int lr_live;
} Jam;

/* 0 opened, 1 the header file is busy, 2 .jhr or .jdx is absent, -1
 * any other error. */
int jam_open(Jam *jam, const char *base, const char *user, int exclusive);
void jam_close(Jam *jam);

/* Lastread calls return 0 on success and -1 on a disk error.
 * -2 means the .jlr user cannot be chosen: there is no record and
 * jamuser is empty, or several records and jamuser is empty. The
 * cursor is remembered and tried again. A record is not invented,
 * because GoldED would not see a CRC it did not write. */
int jam_read_lastread(Jam *jam, uint32_t *msgno, int64_t *mtime);
int jam_write_lastread(Jam *jam, uint32_t msgno);
int jam_msgid_of(Jam *jam, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date);
int jam_find_msgid(Jam *jam, const char *msgid, int64_t target_date, SqHit *hit);

/* JAM CRC-32 of a user name, with the final complement. */
uint32_t jam_user_crc(const char *name);

#endif
