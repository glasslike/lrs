#ifndef LRP_SYNC_H
#define LRP_SYNC_H

#include "config.h"

typedef enum {
    DEC_IGNORE = 0,
    DEC_ACCEPT,
    DEC_KEEP
} Decide;

/*
 * remote_gen is this snapshot's generation.
 * remote_seen is the local generation the peer had already applied
 * when it moved.
 * seen_remote is the highest remote generation we have already decided.
 * local_server is set on the node. An announcement that has never
 * applied our cursor does not move a server that already has one.
 * It replaces the point's position only until the point has applied
 * a server cursor. After that, seen 0 is an ordinary conflict.
 */
/* Next 8.3 counter. 0 and anything past 9999999 become 1, and
 * 9999999 itself is followed by 1. The name is only for the mailer;
 * the cursor generation lives inside the file. */
uint32_t next_snapshot_seq(uint32_t seq);

Decide decide_cursor(uint32_t local_gen, uint32_t seen_remote,
    uint32_t remote_gen, uint32_t remote_seen,
    int64_t local_updated, int64_t remote_updated,
    const char *local_addr, const char *remote_addr,
    int local_server);

int cmd_sync(Config *cfg);
int cmd_show(const Config *cfg);

#endif
