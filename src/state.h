#ifndef LRP_STATE_H
#define LRP_STATE_H

/*
 * Local memory of what we have already told the peer and what we have
 * already accepted from them. This file is ours. GoldED does not read
 * it. The on-disk form is one JSON object ("lrs": 1): sendseq, the
 * name of the snapshot still queued, and one object per echo. Strings
 * use the same byte escaping as a snapshot, so a MSGID with a quote
 * or a newline stays one value. A missing file is an empty memory,
 * which is how the first sync starts. gen grows when the on-disk
 * cursor moves by itself. seen is the peer gen we already knew at
 * that moment. peergen is the highest remote gen we have finished
 * deciding.
 */

#include "util.h"

typedef struct StArea {
    char tag[LRP_TAG];
    char msgid[LRP_MSGID];
    int64_t date;
    uint32_t msgno;
    uint32_t gen;
    uint32_t seen;
    uint32_t peergen;
    int64_t updated;
    int pending;
    char pend_msgid[LRP_MSGID];
    int64_t pend_date;
    uint32_t pend_gen;
    uint32_t pend_seen;
    int64_t pend_updated;
    char pend_from[LRP_ADDR];
    /* Set when import has added this echo but has not yet written the
     * peer cursor into the base (GoldED still holds it). Export must
     * not publish the local lastread until that write succeeds: the
     * local slot may only be a position left by opening the area. */
    int hold;
    /* The point listed this echo and still needs the node's lastread.
     * asked means that request was already placed in a queued file. */
    int ask;
    int asked;
    /* The peer asked for this cursor. The on-disk lastread may already
     * match state (a server baseline, or a cursor published earlier),
     * so export must still queue a snapshot. Cleared once that file
     * is actually attached. */
    int reply;
} StArea;

typedef struct State {
    uint32_t sendseq;
    char queued[LRP_PATH];
    StArea *area;
    size_t n;
} State;

void state_free(State *st);
int state_load(const char *path, State *st);
int state_save(const char *path, const State *st);
StArea *state_area(State *st, const char *tag);

/* Drop every echo for which keep() returns 0. The records that stay
 * keep their order. Sync uses this so a tag removed from the config
 * does not remain in the state file, and does not come back with its
 * old generation if the echo is subscribed again. */
void state_prune(State *st, int (*keep)(const char *tag, void *ctx), void *ctx);

#endif
