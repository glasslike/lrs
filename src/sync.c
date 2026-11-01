#include "sync.h"

#include "lrpfile.h"
#include "base.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

Decide decide_cursor(uint32_t local_gen, uint32_t seen_remote,
    uint32_t remote_gen, uint32_t remote_seen,
    int64_t local_updated, int64_t remote_updated,
    const char *local_addr, const char *remote_addr,
    int local_server)
{
    /* A replay, or a snapshot we already folded in. */
    if (remote_gen == 0 || remote_gen <= seen_remote)
        return DEC_IGNORE;
    /* They moved from a cursor that already includes our latest gen. */
    if (remote_seen >= local_gen)
        return DEC_ACCEPT;
    /* remote_seen == 0: they have never applied a cursor of ours.
     * Opening a rescanned echo makes GoldED write lastread on the way
     * out, so that file is newer than a real earlier reading and must
     * not win on the clock. The server keeps what it already has.
     * The point yields only while it has never applied the server.
     * After that, seen 0 is a concurrent move the server made without
     * having seen us, and the clocks decide. Yielding again would
     * accept their cursor while they accept ours, and both would stop
     * on different messages. */
    if (remote_seen == 0 && local_gen > 0) {
        if (local_server)
            return DEC_KEEP;
        if (seen_remote == 0)
            return DEC_ACCEPT;
    }
    if (remote_updated > local_updated)
        return DEC_ACCEPT;
    if (remote_updated < local_updated)
        return DEC_KEEP;
    /* Equal clocks: the greater FTN address wins on both machines. */
    if (addr_cmp_str(remote_addr, local_addr) > 0)
        return DEC_ACCEPT;
    return DEC_KEEP;
}

/* qico (and binkd) create NNNNFFFF.bsy for the whole session and delete
 * it when the session ends. While it is there the flow file is live:
 * replacing a line under the mailer can drop a file it has already
 * opened, or queue a second copy of a snapshot it is about to send. */
static int bsy_held(const Config *cfg)
{
    return cfg->bsy[0] && path_exists(cfg->bsy);
}

static int flo_line_is(const char *line, const char *filename)
{
    const char *p = line;
    if (*p == '^' || *p == '#' || *p == '~' || *p == '@')
        p++;
    while (*p == ' ' || *p == '"')
        p++;
    return str_icmp(path_base(p), filename) == 0;
}

static int flo_rewrite(const char *flo, const char *drop_name, const char *add_path)
{
    FILE *in;
    FILE *out;
    char tmp[LRP_PATH + 8];
    char line[LRP_LINE];
    int saw = 0;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", flo) >= (int)sizeof tmp)
        return -1;
    in = fopen(flo, "rb");
    out = fopen(tmp, "wb");
    if (!out) {
        if (in)
            fclose(in);
        log_msg("cannot write %s", flo);
        return -1;
    }
    if (in) {
        while (fgets(line, sizeof line, in)) {
            char trimmed[LRP_LINE];
            if (strchr(line, '\n') == NULL && !feof(in)) {
                log_msg("flow line in %s is too long", flo);
                fclose(in);
                fclose(out);
                remove(tmp);
                return -1;
            }
            snprintf(trimmed, sizeof trimmed, "%s", line);
            str_trim(trimmed);
            if (drop_name && drop_name[0] && flo_line_is(trimmed, drop_name))
                continue;
            if (add_path && flo_line_is(trimmed, path_base(add_path)))
                saw = 1;
            if (fputs(line, out) < 0) {
                fclose(in);
                fclose(out);
                remove(tmp);
                return -1;
            }
            if (line[0] && line[strlen(line) - 1] != '\n' && fputc('\n', out) == EOF) {
                fclose(in);
                fclose(out);
                remove(tmp);
                return -1;
            }
        }
        if (ferror(in)) {
            fclose(in);
            fclose(out);
            remove(tmp);
            log_msg("cannot read %s", flo);
            return -1;
        }
        fclose(in);
    }
    if (add_path && !saw && fprintf(out, "^%s\n", add_path) < 0) {
        fclose(out);
        remove(tmp);
        return -1;
    }
    /* Same rule as write_atomic: the previous flow file stays if the
     * new one cannot be put in its place. */
    if (file_sync(out) != 0) {
        fclose(out);
        remove(tmp);
        return -1;
    }
    if (fclose(out) != 0) {
        remove(tmp);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, flo, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, flo) != 0) {
        remove(tmp);
        return -1;
    }
#endif
    return 0;
}

/* 8.3 name: one role letter and seven counter digits.
 * c0000001.lrs is the point, s0000001.lrs is the node. The address is
 * already inside the file, so the name only has to stay unique for the
 * mailer and distinct between the two directions. The counter is
 * zero-padded so the name is always eight characters. After 9999999
 * the next file is 0000001 again: one snapshot is queued at a time,
 * and the previous name has already left outbound. */
static int snapshot_name(const Config *cfg, uint32_t seq, char *dst, size_t cap)
{
    if (seq == 0 || seq > 9999999u)
        return -1;
    if (snprintf(dst, cap, "%c%07u.lrs", cfg->server ? 's' : 'c', seq) >= (int)cap)
        return -1;
    return 0;
}

uint32_t next_snapshot_seq(uint32_t seq)
{
    if (seq == 0 || seq >= 9999999u)
        return 1;
    return seq + 1;
}

static void adopt(StArea *st, const char *msgid, int64_t date, uint32_t msgno, int64_t updated, uint32_t peergen)
{
    snprintf(st->msgid, sizeof st->msgid, "%s", msgid && msgid[0] ? msgid : "-");
    st->date = date;
    st->msgno = msgno;
    st->updated = updated;
    st->peergen = peergen;
    st->pending = 0;
    st->hold = 0;
    st->ask = 0;
    st->asked = 0;
    st->reply = 0;
    st->pend_msgid[0] = '\0';
}

static void park(StArea *st, const char *from, const char *msgid, int64_t date,
    uint32_t gen, uint32_t seen, int64_t updated)
{
    st->pending = 1;
    snprintf(st->pend_msgid, sizeof st->pend_msgid, "%s", msgid);
    st->pend_date = date;
    st->pend_gen = gen;
    st->pend_seen = seen;
    st->pend_updated = updated;
    snprintf(st->pend_from, sizeof st->pend_from, "%s", from ? from : "");
    /* The cursor is not in .sql yet. Leave ask set so export does not
     * publish the local lastread before this position is written.
     * asked stays set too: the request was already queued once. */
    st->reply = 0;
}

/* 0 when msgno is in the lastread file, 1 when there is no JAM user
 * to attach it to yet (the cursor is parked), -1 on a disk error.
 * The base is closed on both failures. */
static int put_lastread(Base *base, const CfgArea *area, StArea *st,
    const char *from, const char *msgid, int64_t date, uint32_t msgno,
    uint32_t gen, uint32_t seen, int64_t updated)
{
    int rc = base_write_lastread(base, msgno);
    if (rc == -2) {
        base_close(base);
        log_msg("%s: JAM user is unknown, cursor deferred", area->tag);
        park(st, from, msgid, date, gen, seen, updated);
        return 1;
    }
    if (rc != 0) {
        base_close(base);
        return -1;
    }
    return 0;
}

static int apply_remote(const Config *cfg, const CfgArea *area, StArea *st,
    const char *from, const char *msgid, int64_t date, uint32_t gen, uint32_t seen, int64_t updated,
    int request)
{
    Decide d;
    Base base;
    int opened;
    SqHit hit;
    /* A request is not a cursor. The server has already baselined its
     * own lastread. That position matches state, so export would
     * otherwise see no movement and queue nothing. reply forces the
     * same sync to attach the stored cursor. */
    if (request) {
        log_msg("%s: position requested", area->tag);
        if (cfg->server && st->gen > 0)
            st->reply = 1;
        return 0;
    }
    d = decide_cursor(st->gen, st->peergen, gen, seen, st->updated, updated, cfg->address, from, cfg->server);
    /* A replay or a generation we already folded in. Not logged: a
     * full snapshot repeats every area, and the unchanged ones are
     * the common case. */
    if (d == DEC_IGNORE)
        return 0;
    if (d == DEC_KEEP) {
        log_msg("%s: keep local cursor, remote gen %u loses", area->tag, gen);
        st->peergen = gen;
        st->pending = 0;
        /* The server is keeping its own cursor, so export may publish
         * it. A downlink still waiting to write the peer cursor stays held. */
        if (cfg->server)
            st->hold = 0;
        /* Their snapshot does not include this cursor, or they would
         * have been ignored or accepted above. Export must send it
         * again. Import may already have copied the on-disk move into
         * state, so a plain "msgid matches" check would stay silent
         * and the other side would keep the loser. */
        if (st->gen > 0)
            st->reply = 1;
        return 0;
    }
    opened = base_open(&base, area->path, cfg->squishuser, cfg->jamuser, 1);
    if (opened == 1) {
        log_msg("%s: base is open, defer gen %u", area->tag, gen);
        park(st, from, msgid, date, gen, seen, updated);
        return 0;
    }
    /* The base was deleted (the point unsubscribed). Skipping it keeps
     * the rest of the snapshot. The config line is removed separately. */
    if (opened == 2) {
        log_msg("%s: base is gone", area->tag);
        return 0;
    }
    if (opened != 0)
        return -1;
    if (strcmp(msgid, "-") == 0) {
        int wr = put_lastread(&base, area, st, from, msgid, date, 0, gen, seen, updated);
        if (wr != 0)
            return wr < 0 ? -1 : 0;
        adopt(st, "-", -1, 0, updated, gen);
        base_close(&base);
        log_msg("%s: cursor set before the first message", area->tag);
        return 0;
    }
    memset(&hit, 0, sizeof hit);
    {
        int found = base_find_msgid(&base, msgid, date, &hit);
        if (found == -2) {
            base_close(&base);
            log_msg("%s: JAM user is unknown, cursor deferred", area->tag);
            park(st, from, msgid, date, gen, seen, updated);
            return 0;
        }
        if (found != 0) {
            base_close(&base);
            return -1;
        }
    }
    if (hit.found) {
        int wr = put_lastread(&base, area, st, from, msgid, date, hit.msgno, gen, seen, updated);
        if (wr != 0)
            return wr < 0 ? -1 : 0;
        adopt(st, msgid, hit.date >= 0 ? hit.date : date, hit.msgno, updated, gen);
        base_close(&base);
        log_msg("%s: cursor -> msg %u (%s)", area->tag, hit.msgno, msgid);
        return 0;
    }
    /* Older than everything still stored: the message was packed away.
     * Land on the nearest survivor and remember that message, so the
     * next export does not announce the snap as a new reading session. */
    if (date >= 0 && hit.oldest >= 0 && date < hit.oldest && hit.have_fallback) {
        int wr = put_lastread(&base, area, st, from, msgid, date, hit.fallback_msgno,
            gen, seen, updated);
        if (wr != 0)
            return wr < 0 ? -1 : 0;
        adopt(st, hit.fallback_msgid, hit.fallback_date, hit.fallback_msgno, updated, gen);
        base_close(&base);
        log_msg("%s: %s is gone, cursor -> msg %u", area->tag, msgid, hit.fallback_msgno);
        return 0;
    }
    base_close(&base);
    log_msg("%s: %s is not in the base yet", area->tag, msgid);
    park(st, from, msgid, date, gen, seen, updated);
    return 0;
}

/* A subscribed echo the point still has, with no cursor of its own yet.
 * gen 0 is not a position. The server uses the tag to keep the echo. */
static int add_listed(LrpFile *snap, const char *tag)
{
    LrpArea rec;
    memset(&rec, 0, sizeof rec);
    snprintf(rec.tag, sizeof rec.tag, "%s", tag);
    snprintf(rec.msgid, sizeof rec.msgid, "-");
    rec.date = -1;
    rec.gen = 0;
    rec.seen = 0;
    rec.updated = -1;
    rec.request = 0;
    return lrp_file_add(snap, &rec);
}

/* Publish local cursor moves and a position request that has not
 * been queued yet. A request is sent once. The point also lists every
 * echo it still has, so the server can drop one that disappeared.
 * force writes a snapshot even when no cursor moved: the point removed
 * an echo and the server has to see the new list. */
static void state_drop_unconfigured(State *st, const Config *cfg);

static int cmd_export(const Config *cfg, int force)
{
    State st;
    LrpFile snap;
    size_t i;
    int changed = 0;
    char *text = NULL;
    char name[LRP_PATH];
    char full[LRP_PATH];
    uint32_t seq;
    if (state_load(cfg->state, &st) != 0)
        return 1;
    memset(&snap, 0, sizeof snap);
    snprintf(snap.from, sizeof snap.from, "%s", cfg->address);
    for (i = 0; i < cfg->n; i++) {
        Base base;
        StArea *sa;
        uint32_t msgno = 0;
        int64_t mtime = 0;
        char msgid[LRP_MSGID];
        int64_t date = -1;
        int rc;
        LrpArea rec;
        sa = state_area(&st, cfg->area[i].tag);
        if (!sa) {
            state_free(&st);
            return 1;
        }
        /* The echo was learned from a snapshot and that cursor is not
         * in the base yet. The on-disk lastread is whatever GoldED
         * wrote on the way out, which is not a reading session. */
        if (sa->hold) {
            log_msg("%s: held until the imported cursor is written", cfg->area[i].tag);
            if (!cfg->server && add_listed(&snap, sa->tag) != 0) {
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            continue;
        }
        /* This echo is in the config and no cursor has been exchanged
         * yet. Ask the node for its lastread. The local slot is not
         * published: after a rescan it is often only an open-and-close. */
        if (!cfg->server && sa->gen == 0 && sa->peergen == 0 && sa->updated < 0)
            sa->ask = 1;
        if (sa->ask) {
            /* Already placed in a queued file. Do not ask again on
             * later syncs: the node answers that one request. If this
             * sync writes a snapshot for some other reason, the request
             * stays in it so a replaced unsent file does not drop it. */
            if (!sa->asked)
                changed = 1;
            memset(&rec, 0, sizeof rec);
            snprintf(rec.tag, sizeof rec.tag, "%s", sa->tag);
            snprintf(rec.msgid, sizeof rec.msgid, "-");
            rec.date = -1;
            rec.gen = 0;
            rec.seen = 0;
            rec.updated = -1;
            rec.request = 1;
            if (lrp_file_add(&snap, &rec) != 0) {
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            if (!sa->asked)
                log_msg("%s: request position", cfg->area[i].tag);
            continue;
        }
        rc = base_open(&base, cfg->area[i].path, cfg->squishuser, cfg->jamuser, 0);
        if (rc == 2) {
            log_msg("%s: base is gone", cfg->area[i].tag);
            continue;
        }
        if (rc != 0) {
            /* Still subscribed. Leaving it out of a point snapshot
             * would look like an unsubscribe on the server. */
            log_msg("skip %s", cfg->area[i].tag);
            if (!cfg->server && add_listed(&snap, sa->tag) != 0) {
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            continue;
        }
        if (base_read_lastread(&base, &msgno, &mtime) != 0) {
            base_close(&base);
            log_msg("%s: cannot read lastread", cfg->area[i].tag);
            if (!cfg->server && add_listed(&snap, sa->tag) != 0) {
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            continue;
        }
        msgid[0] = '\0';
        rc = base_msgid_of(&base, msgno, msgid, sizeof msgid, &date);
        base_close(&base);
        /* A cursor we just took from the peer matches state while gen
         * is still 0. Publishing it again would look like a new local
         * reading. Wait until the msgno or MSGID actually changes. */
        if (rc != 0) {
            log_msg("%s: message %u has no MSGID, leave the published cursor", cfg->area[i].tag, msgno);
        } else if (!(sa->msgid[0] && sa->msgno == msgno && strcmp(sa->msgid, msgid) == 0)) {
            if (sa->gen == 0xffffffffu) {
                log_msg("%s: generation counter wrapped", cfg->area[i].tag);
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            sa->gen++;
            sa->seen = sa->peergen;
            sa->msgno = msgno;
            snprintf(sa->msgid, sizeof sa->msgid, "%s", msgid);
            sa->date = date;
            sa->updated = mtime > 0 ? mtime : (int64_t)time(NULL);
            sa->pending = 0;
            changed = 1;
            log_msg("%s: local cursor gen %u msgid %s", cfg->area[i].tag, sa->gen, sa->msgid);
        }
        /* The peer asked for a cursor we already stored. Include it
         * without bumping gen: the lastread itself did not move. */
        if (sa->reply && sa->gen > 0) {
            changed = 1;
            log_msg("%s: send stored cursor gen %u", cfg->area[i].tag, sa->gen);
        }
        if (sa->gen == 0) {
            /* Still subscribed. Omit it and the server would treat the
             * echo as gone the next time this file is the client's list. */
            if (!cfg->server && add_listed(&snap, sa->tag) != 0) {
                state_free(&st);
                lrp_file_free(&snap);
                return 1;
            }
            continue;
        }
        memset(&rec, 0, sizeof rec);
        snprintf(rec.tag, sizeof rec.tag, "%s", sa->tag);
        snprintf(rec.msgid, sizeof rec.msgid, "%s", sa->msgid[0] ? sa->msgid : "-");
        rec.date = sa->date;
        rec.gen = sa->gen;
        rec.seen = sa->seen;
        rec.updated = sa->updated;
        if (lrp_file_add(&snap, &rec) != 0) {
            state_free(&st);
            lrp_file_free(&snap);
            return 1;
        }
    }
    if (snap.n == 0 && !force) {
        log_msg("nothing to publish");
        state_free(&st);
        return 0;
    }
    /* The number in the filename exists so a mailer never sees the same
     * name and size twice for two different snapshots. It advances only
     * when a cursor actually moved, and after 9999999 it starts at 1
     * again. An unchanged snapshot is not queued again after the
     * previous file has already been sent. */
    if (!changed && !force) {
        int still_queued = 0;
        char queued_name[LRP_PATH];
        queued_name[0] = '\0';
        if (st.queued[0]) {
            char queued_path[LRP_PATH];
            snprintf(queued_name, sizeof queued_name, "%s", st.queued);
            if (path_join(queued_path, sizeof queued_path, cfg->outbound, st.queued) == 0
                && path_exists(queued_path))
                still_queued = 1;
        }
        state_free(&st);
        if (still_queued)
            log_msg("outbound already has %s", queued_name);
        else
            log_msg("no cursor change");
        return 0;
    }
    text = lrp_file_format(&snap);
    lrp_file_free(&snap);
    if (!text) {
        state_free(&st);
        return 1;
    }
    seq = next_snapshot_seq(st.sendseq);
    if (snapshot_name(cfg, seq, name, sizeof name) != 0
        || path_join(full, sizeof full, cfg->outbound, name) != 0) {
        free(text);
        state_free(&st);
        return 1;
    }
    if (bsy_held(cfg)) {
        log_msg("link is busy, leave the queued file alone");
        free(text);
        state_free(&st);
        return 0;
    }
    /* A node flow file sits in outbound itself. A point flow file sits
     * in outbound/NNNNFFFF.pnt, which does not exist until the first
     * attach for that point. The .lrs payload stays in outbound; only
     * the flow file needs the extra directory. */
    if (path_ensure_parent(cfg->flo) != 0) {
        log_msg("cannot create directory for %s", cfg->flo);
        free(text);
        state_free(&st);
        return 1;
    }
    if (write_atomic(full, text) != 0) {
        log_msg("cannot write %s", full);
        free(text);
        state_free(&st);
        return 1;
    }
    free(text);
    if (flo_rewrite(cfg->flo, st.queued, full) != 0) {
        remove(full);
        state_free(&st);
        return 1;
    }
    /* The new file can reuse a name only after a full cycle. Never
     * delete it as "the previous snapshot" in that case. */
    if (st.queued[0] && strcmp(st.queued, name) != 0) {
        char oldp[LRP_PATH];
        if (path_join(oldp, sizeof oldp, cfg->outbound, st.queued) == 0)
            remove(oldp);
    }
    st.sendseq = seq;
    snprintf(st.queued, sizeof st.queued, "%s", name);
    for (i = 0; i < st.n; i++) {
        if (st.area[i].ask)
            st.area[i].asked = 1;
        /* Held areas were not part of this file. A reply on any other
         * area was included, because every published cursor goes out. */
        if (!st.area[i].hold)
            st.area[i].reply = 0;
    }
    state_drop_unconfigured(&st, cfg);
    if (state_save(cfg->state, &st) != 0) {
        log_msg("cannot save %s", cfg->state);
        state_free(&st);
        return 1;
    }
    state_free(&st);
    log_msg("queued %s via %s", name, cfg->flo);
    return 0;
}

/* c0000001.lrs or s0000001.lrs, either case. A mailer that renames a
 * collision appends another suffix (c0000001.lrs.1), so ".lrs" need
 * not be the very end of the directory entry. */
static int is_snapshot_name(const char *name)
{
    size_t i;
    char role;
    if (!name)
        return 0;
    role = name[0];
    if (role != 'c' && role != 'C' && role != 's' && role != 'S')
        return 0;
    for (i = 1; i <= 7; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
    }
    if (name[8] != '.')
        return 0;
    if (!((name[9] == 'l' || name[9] == 'L')
            && (name[10] == 'r' || name[10] == 'R')
            && (name[11] == 's' || name[11] == 'S')))
        return 0;
    return name[12] == '\0' || name[12] == '.';
}

/* Seven digits after the role letter. 0 when the name is not ours. */
static uint32_t snapshot_seq(const char *name)
{
    uint32_t v = 0;
    size_t i;
    if (!is_snapshot_name(name))
        return 0;
    for (i = 1; i <= 7; i++)
        v = v * 10u + (uint32_t)(name[i] - '0');
    return v;
}

typedef struct Cand {
    char from[LRP_ADDR];
    LrpArea area;
} Cand;

static int better_cand(const Cand *a, const Cand *b)
{
    if (a->area.gen != b->area.gen)
        return a->area.gen > b->area.gen;
    if (a->area.updated != b->area.updated)
        return a->area.updated > b->area.updated;
    return addr_cmp_str(a->from, b->from) > 0;
}

/* Record the on-disk lastread as generation 1 before applying a peer
 * that has never seen us. Without this, a server area that is not in
 * the state yet looks unpublished (gen 0) and the downlink's first
 * snapshot would be taken as the initial cursor. Message 0 is left
 * unpublished: the area has not been read, so the peer may set it. */
static int baseline_server(const Config *cfg, State *st)
{
    size_t i;
    if (!cfg->server)
        return 0;
    for (i = 0; i < cfg->n; i++) {
        StArea *sa;
        Base base;
        uint32_t msgno = 0;
        int64_t mtime = 0;
        char msgid[LRP_MSGID];
        int64_t date = -1;
        int rc;
        sa = state_area(st, cfg->area[i].tag);
        if (!sa)
            return -1;
        if (sa->gen > 0 || sa->hold)
            continue;
        rc = base_open(&base, cfg->area[i].path, cfg->squishuser, cfg->jamuser, 0);
        if (rc == 2)
            continue;
        if (rc != 0) {
            log_msg("%s: cannot read local cursor to baseline", cfg->area[i].tag);
            continue;
        }
        if (base_read_lastread(&base, &msgno, &mtime) != 0) {
            base_close(&base);
            return -1;
        }
        msgid[0] = '\0';
        if (msgno == 0 || base_msgid_of(&base, msgno, msgid, sizeof msgid, &date) != 0) {
            base_close(&base);
            continue;
        }
        base_close(&base);
        sa->gen = 1;
        sa->seen = sa->peergen;
        sa->msgno = msgno;
        snprintf(sa->msgid, sizeof sa->msgid, "%s", msgid);
        sa->date = date;
        sa->updated = mtime > 0 ? mtime : (int64_t)time(NULL);
        log_msg("%s: server baseline gen 1 msgid %s", cfg->area[i].tag, sa->msgid);
    }
    return 0;
}

/* Fold an on-disk lastread into state before a remote cursor is
 * decided. Export notices the same move when it publishes, but import
 * runs first. Until this copy happens, state still names the previous
 * exchange, and every inbound snapshot looks newer than a reading
 * that is already in the lastread file. A virgin area (nothing
 * exchanged, updated still unset) is left alone so the point asks
 * for the server cursor instead of publishing a rescan.
 * Returns 1 when a generation was recorded, 0 when the disk matches
 * or this area is not ready, -1 when the generation counter is full. */
static int note_local_cursor(const Config *cfg, const CfgArea *area, StArea *sa)
{
    Base base;
    uint32_t msgno = 0;
    int64_t mtime = 0;
    char msgid[LRP_MSGID];
    int64_t date = -1;
    int rc;
    if (sa->hold || sa->ask || sa->pending)
        return 0;
    if (sa->gen == 0 && sa->peergen == 0 && sa->updated < 0)
        return 0;
    rc = base_open(&base, area->path, cfg->squishuser, cfg->jamuser, 0);
    if (rc == 2)
        return 0;
    if (rc != 0) {
        log_msg("skip %s", area->tag);
        return 0;
    }
    if (base_read_lastread(&base, &msgno, &mtime) != 0) {
        base_close(&base);
        log_msg("%s: cannot read lastread", area->tag);
        return 0;
    }
    msgid[0] = '\0';
    rc = base_msgid_of(&base, msgno, msgid, sizeof msgid, &date);
    base_close(&base);
    if (rc != 0) {
        log_msg("%s: message %u has no MSGID, leave the published cursor", area->tag, msgno);
        return 0;
    }
    if (sa->msgid[0] && sa->msgno == msgno && strcmp(sa->msgid, msgid) == 0)
        return 0;
    if (sa->gen == 0xffffffffu) {
        log_msg("%s: generation counter wrapped", area->tag);
        return -1;
    }
    sa->gen++;
    sa->seen = sa->peergen;
    sa->msgno = msgno;
    snprintf(sa->msgid, sizeof sa->msgid, "%s", msgid);
    sa->date = date;
    sa->updated = mtime > 0 ? mtime : (int64_t)time(NULL);
    sa->pending = 0;
    /* Export compares state with the file. They match now, so the
     * generation would never leave this machine. reply attaches it
     * once. Accepting a remote cursor clears reply. */
    sa->reply = 1;
    log_msg("%s: local cursor gen %u msgid %s", area->tag, sa->gen, sa->msgid);
    return 1;
}

/* Apply inbound snapshots and, on the point, list new bases.
 * Called only from cmd_sync. */
static int cmd_import(Config *cfg)
{
    State st;
    StrList names;
    Cand *cands = NULL;
    size_t nc = 0;
    size_t cap = 0;
    char **accepted = NULL;
    size_t na = 0;
    size_t i, k;
    size_t known;
    memset(&names, 0, sizeof names);
    if (state_load(cfg->state, &st) != 0)
        return 1;
    /* The point records every base it has and marks it ask. cmd_sync
     * then exports, which queues the position request in the same run.
     * The node does not scan: a new echo reaches its config only inside
     * that request. */
    known = cfg->n;
    if (!cfg->server && config_discover(cfg) != 0) {
        state_free(&st);
        return 1;
    }
    for (i = known; i < cfg->n; i++) {
        StArea *sa = state_area(&st, cfg->area[i].tag);
        if (!sa) {
            state_free(&st);
            return 1;
        }
        sa->ask = 1;
        log_msg("%s: added, will request position", cfg->area[i].tag);
    }
    if (dir_list(cfg->inbound, &names) != 0) {
        log_msg("cannot read inbound %s", cfg->inbound);
        state_free(&st);
        return 1;
    }
    for (i = 0; i < names.n; i++) {
        char full[LRP_PATH];
        char *text;
        LrpFile file;
        size_t a;
        if (!is_snapshot_name(names.item[i]))
            continue;
        if (path_join(full, sizeof full, cfg->inbound, names.item[i]) != 0)
            continue;
        text = read_all(full, NULL);
        if (!text)
            continue;
        if (lrp_file_parse(text, &file) != 0) {
            log_msg("skip %s (not an LRP snapshot)", names.item[i]);
            free(text);
            continue;
        }
        free(text);
        if (str_icmp(file.from, cfg->peer) != 0) {
            log_msg("skip %s from %s", names.item[i], file.from);
            lrp_file_free(&file);
            continue;
        }
        for (a = 0; a < file.n; a++) {
            if (nc == cap) {
                Cand *grown;
                cap = cap ? cap * 2 : 16;
                grown = (Cand *)realloc(cands, cap * sizeof(Cand));
                if (!grown) {
                    size_t z;
                    lrp_file_free(&file);
                    free(cands);
                    for (z = 0; z < na; z++)
                        free(accepted[z]);
                    free(accepted);
                    strlist_free(&names);
                    state_free(&st);
                    return 1;
                }
                cands = grown;
            }
            memset(&cands[nc], 0, sizeof(Cand));
            snprintf(cands[nc].from, sizeof cands[nc].from, "%s", file.from);
            cands[nc].area = file.area[a];
            nc++;
        }
        lrp_file_free(&file);
        {
            char **grown = (char **)realloc(accepted, (na + 1) * sizeof(char *));
            char *copy;
            size_t z;
            if (!grown) {
                free(cands);
                for (z = 0; z < na; z++)
                    free(accepted[z]);
                free(accepted);
                strlist_free(&names);
                state_free(&st);
                return 1;
            }
            accepted = grown;
            copy = str_dup(names.item[i]);
            if (!copy) {
                free(cands);
                for (z = 0; z < na; z++)
                    free(accepted[z]);
                free(accepted);
                strlist_free(&names);
                state_free(&st);
                return 1;
            }
            accepted[na++] = copy;
        }
    }
    /* The newest client snapshot is the subscription list. An older
     * file still in inbound must not keep an echo the point already
     * dropped, and must not add it back. The Squish base stays. */
    LrpFile member;
    int have_member = 0;
    memset(&member, 0, sizeof member);
    if (cfg->server && na > 0) {
        size_t bi = 0;
        uint32_t best = 0;
        char full[LRP_PATH];
        char *text = NULL;
        for (k = 0; k < na; k++) {
            uint32_t seq = snapshot_seq(accepted[k]);
            if (k == 0 || seq >= best) {
                best = seq;
                bi = k;
            }
        }
        if (path_join(full, sizeof full, cfg->inbound, accepted[bi]) != 0
            || (text = read_all(full, NULL)) == NULL
            || lrp_file_parse(text, &member) != 0) {
            free(text);
            lrp_file_free(&member);
            free(cands);
            for (i = 0; i < na; i++)
                free(accepted[i]);
            free(accepted);
            strlist_free(&names);
            state_free(&st);
            return 1;
        }
        free(text);
        have_member = 1;
        i = 0;
        while (i < cfg->n) {
            int keep = 0;
            for (k = 0; k < member.n; k++) {
                if (str_icmp(member.area[k].tag, cfg->area[i].tag) == 0) {
                    keep = 1;
                    break;
                }
            }
            if (keep) {
                i++;
                continue;
            }
            log_msg("%s: client no longer lists it, removed from config", cfg->area[i].tag);
            if (config_forget_area(cfg, cfg->area[i].tag) != 0) {
                lrp_file_free(&member);
                free(cands);
                for (k = 0; k < na; k++)
                    free(accepted[k]);
                free(accepted);
                strlist_free(&names);
                state_free(&st);
                return 1;
            }
        }
    }
    /* A tag in the snapshot that we do not list is added when its
     * Squish or JAM base is sitting in echodir. Areas with no base
     * are left out of this run.
     * On the server, only a tag in the newest client list is added. */
    for (k = 0; k < nc; k++) {
        int adopted;
        if (have_member) {
            int listed = 0;
            size_t m;
            for (m = 0; m < member.n; m++) {
                if (str_icmp(member.area[m].tag, cands[k].area.tag) == 0) {
                    listed = 1;
                    break;
                }
            }
            if (!listed)
                continue;
        }
        if (config_has_area(cfg, cands[k].area.tag))
            continue;
        adopted = config_adopt_area(cfg, cands[k].area.tag);
        if (adopted < 0) {
            lrp_file_free(&member);
            free(cands);
            for (i = 0; i < na; i++)
                free(accepted[i]);
            free(accepted);
            strlist_free(&names);
            state_free(&st);
            return 1;
        }
        /* Newly listed. Hold export until apply_remote writes the peer
         * cursor. If the base is busy, the hold stays with the parked
         * snapshot and the local lastread is not published. */
        if (adopted == 0 && !cfg->server) {
            StArea *sa = state_area(&st, cands[k].area.tag);
            if (!sa) {
                lrp_file_free(&member);
                free(cands);
                for (i = 0; i < na; i++)
                    free(accepted[i]);
                free(accepted);
                strlist_free(&names);
                state_free(&st);
                return 1;
            }
            sa->hold = 1;
        }
    }
    if (baseline_server(cfg, &st) != 0) {
        lrp_file_free(&member);
        free(cands);
        for (i = 0; i < na; i++)
            free(accepted[i]);
        free(accepted);
        strlist_free(&names);
        state_free(&st);
        return 1;
    }
    for (i = 0; i < cfg->n; i++) {
        StArea *sa = state_area(&st, cfg->area[i].tag);
        Cand best;
        int have = 0;
        if (!sa) {
            lrp_file_free(&member);
            free(cands);
            for (k = 0; k < na; k++)
                free(accepted[k]);
            free(accepted);
            strlist_free(&names);
            state_free(&st);
            return 1;
        }
        /* The lastread may have moved since the previous sync. Decide
         * against that position, not against the generation we last
         * published. */
        if (note_local_cursor(cfg, &cfg->area[i], sa) < 0) {
            lrp_file_free(&member);
            free(cands);
            for (k = 0; k < na; k++)
                free(accepted[k]);
            free(accepted);
            strlist_free(&names);
            state_free(&st);
            return 1;
        }
        memset(&best, 0, sizeof best);
        for (k = 0; k < nc; k++) {
            if (str_icmp(cands[k].area.tag, cfg->area[i].tag) != 0)
                continue;
            if (!have || better_cand(&cands[k], &best)) {
                best = cands[k];
                have = 1;
            }
        }
        if (sa->pending) {
            Cand pend;
            memset(&pend, 0, sizeof pend);
            snprintf(pend.from, sizeof pend.from, "%s", sa->pend_from);
            snprintf(pend.area.tag, sizeof pend.area.tag, "%s", sa->tag);
            snprintf(pend.area.msgid, sizeof pend.area.msgid, "%s", sa->pend_msgid);
            pend.area.date = sa->pend_date;
            pend.area.gen = sa->pend_gen;
            pend.area.seen = sa->pend_seen;
            pend.area.updated = sa->pend_updated;
            if (!have || better_cand(&pend, &best)) {
                best = pend;
                have = 1;
            }
        }
        if (!have)
            continue;
        if (apply_remote(cfg, &cfg->area[i], sa, best.from, best.area.msgid,
                best.area.date, best.area.gen, best.area.seen, best.area.updated,
                best.area.request) != 0) {
            lrp_file_free(&member);
            free(cands);
            for (k = 0; k < na; k++)
                free(accepted[k]);
            free(accepted);
            strlist_free(&names);
            state_free(&st);
            return 1;
        }
    }
    state_drop_unconfigured(&st, cfg);
    if (state_save(cfg->state, &st) != 0) {
        log_msg("cannot save %s", cfg->state);
        lrp_file_free(&member);
        free(cands);
        for (k = 0; k < na; k++)
            free(accepted[k]);
        free(accepted);
        strlist_free(&names);
        state_free(&st);
        return 1;
    }
    for (k = 0; k < na; k++) {
        char full[LRP_PATH];
        if (path_join(full, sizeof full, cfg->inbound, accepted[k]) == 0) {
            if (remove(full) == 0)
                log_msg("removed %s", accepted[k]);
            else
                log_msg("cannot remove %s", accepted[k]);
        }
        free(accepted[k]);
    }
    free(accepted);
    free(cands);
    strlist_free(&names);
    state_free(&st);
    lrp_file_free(&member);
    if (na == 0)
        log_msg("no snapshots in %s", cfg->inbound);
    return 0;
}

static int state_tag_configured(const char *tag, void *ctx)
{
    return config_has_area((const Config *)ctx, tag);
}

/* The state file is not a history. A tag that left the config has no
 * base to apply, and keeping its generation would make a later
 * resubscribe look like an echo that had already exchanged a cursor. */
static void state_drop_unconfigured(State *st, const Config *cfg)
{
    size_t i;
    for (i = 0; i < st->n; i++) {
        if (!config_has_area(cfg, st->area[i].tag))
            log_msg("%s: removed from state", st->area[i].tag);
    }
    state_prune(st, state_tag_configured, (void *)cfg);
}

/* The folder that holds the base is still there, but neither a Squish
 * pair (.sqd and .sqi) nor a JAM pair (.jhr and .jdx) is. That is an
 * unsubscribed echo, not a missing disk. A missing folder is left
 * alone: the path may be a drive that is not mounted. A JAM area is
 * not unsubscribed merely because it has no .sqd. */
static int base_unsubscribed(const char *base)
{
    return base_present(base) == 0;
}

/* Drop echoarea lines whose bases were deleted. Done before import so
 * a snapshot for that tag is not applied and does not fail the run. */
static int drop_unsubscribed(Config *cfg)
{
    size_t i = 0;
    int removed = 0;
    while (i < cfg->n) {
        char tag[LRP_TAG];
        if (!base_unsubscribed(cfg->area[i].path)) {
            i++;
            continue;
        }
        snprintf(tag, sizeof tag, "%s", cfg->area[i].tag);
        log_msg("%s: base is gone, removed from config", tag);
        if (config_forget_area(cfg, tag) != 0)
            return -1;
        removed++;
    }
    return removed;
}

/* Import first, so a cursor in the inbound snapshot is applied before
 * we publish. Export then queues one file: local moves, and position
 * requests for echoes this import just listed. Run after the tosser
 * and before the mailer. */
int cmd_sync(Config *cfg)
{
    int dropped;
    int rc;
    dropped = drop_unsubscribed(cfg);
    if (dropped < 0)
        return 1;
    rc = cmd_import(cfg);
    if (rc != 0)
        return rc;
    /* Only the point's file is the subscription list. A removed echo
     * has to be written out even when no cursor moved, or the server
     * never learns that the list shrank. */
    return cmd_export(cfg, dropped > 0 && !cfg->server);
}

int cmd_show(const Config *cfg)
{
    size_t i;
    for (i = 0; i < cfg->n; i++) {
        Base base;
        uint32_t msgno = 0;
        char msgid[LRP_MSGID];
        int64_t date = -1;
        if (base_open(&base, cfg->area[i].path, cfg->squishuser, cfg->jamuser, 0) != 0)
            continue;
        base_read_lastread(&base, &msgno, NULL);
        msgid[0] = '\0';
        if (base_msgid_of(&base, msgno, msgid, sizeof msgid, &date) != 0)
            snprintf(msgid, sizeof msgid, "(no MSGID)");
        base_close(&base);
        printf("%s  msg %u  %s\n", cfg->area[i].tag, msgno, msgid);
    }
    return 0;
}
