#ifndef LRP_LRPFILE_H
#define LRP_LRPFILE_H

/*
 * On-disk snapshot. One file is a full picture of every area the
 * sender publishes, not a delta.
 *
 * The writer emits one JSON object. Each echo is one object in "echo".
 * Strings are JSON-escaped, so a MSGID may contain quotes, backslashes
 * and control bytes without changing the shape of the file. A byte
 * outside printable ASCII is written as \u00XX and read back as that
 * byte. "lrp" is 2.
 *
 *   {
 *     "lrp": 2,
 *     "from": "2:203/910.1",
 *     "echo": [
 *       {"tag":"RU.GOLDED","gen":14,"seen":9,"date":"1997-08-19T12:00:00Z","updated":"2026-10-30T17:40:12Z","request":0,"msgid":"<1187509996@ddt.demos.su> 93d75b8f"}
 *     ]
 *   }
 *
 * msgid "-" means the cursor is before the first message. The msgid
 * value is opaque: it is not parsed as an FTN address.
 * request 1 asks the peer for its cursor and carries no position.
 * gen is 0 on a request, so it cannot win a conflict.
 */

#include "util.h"

typedef struct LrpArea {
    char tag[LRP_TAG];
    char msgid[LRP_MSGID];
    int64_t date;
    uint32_t gen;
    uint32_t seen;
    int64_t updated;
    /* 1: the sender has the area and wants the peer's lastread.
     * The other fields are not a cursor. */
    int request;
} LrpArea;

typedef struct LrpFile {
    char from[LRP_ADDR];
    LrpArea *area;
    size_t n;
} LrpFile;

void lrp_file_free(LrpFile *f);
int lrp_file_parse(const char *text, LrpFile *out);
int lrp_file_add(LrpFile *f, const LrpArea *area);
char *lrp_file_format(const LrpFile *f);

#endif
