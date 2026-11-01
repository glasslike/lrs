#ifndef LRP_BASE_H
#define LRP_BASE_H

/*
 * One echo on this machine. The other side may store the same echo in
 * the other format: the snapshot carries a MSGID, not a message number
 * and not a base type. Which files exist next to the stem decides.
 * A stem that has both a Squish pair and a JAM pair is refused, because
 * either lastread could be the one GoldED is showing.
 */

#include "jam.h"

enum {
    BASE_SQUISH = 1,
    BASE_JAM = 2
};

typedef struct Base {
    int kind;
    Sq sq;
    Jam jam;
} Base;

/* 0 opened, 1 busy, 2 neither base is on disk, -1 error (including
 * both formats present). */
int base_open(Base *b, const char *stem, int squishuser, const char *jamuser,
    int exclusive);
void base_close(Base *b);

int base_read_lastread(Base *b, uint32_t *msgno, int64_t *mtime);
int base_write_lastread(Base *b, uint32_t msgno);
int base_msgid_of(Base *b, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date);
int base_find_msgid(Base *b, const char *msgid, int64_t target_date, SqHit *hit);

/* 1 when a complete Squish or JAM base is here, 0 when the parent
 * directory exists but neither base does, -1 when the path is empty
 * or its parent directory is missing (an unmounted drive). */
int base_present(const char *stem);

#endif
