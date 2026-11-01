#include "state.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void state_free(State *st)
{
    if (!st)
        return;
    free(st->area);
    st->area = NULL;
    st->n = 0;
}

StArea *state_area(State *st, const char *tag)
{
    size_t i;
    StArea *grown;
    for (i = 0; i < st->n; i++) {
        if (str_icmp(st->area[i].tag, tag) == 0)
            return &st->area[i];
    }
    grown = (StArea *)realloc(st->area, (st->n + 1) * sizeof(StArea));
    if (!grown)
        return NULL;
    st->area = grown;
    memset(&st->area[st->n], 0, sizeof(StArea));
    st->area[st->n].date = -1;
    st->area[st->n].updated = -1;
    st->area[st->n].pend_date = -1;
    st->area[st->n].pend_updated = -1;
    snprintf(st->area[st->n].tag, sizeof st->area[st->n].tag, "%s", tag);
    st->n++;
    return &st->area[st->n - 1];
}

void state_prune(State *st, int (*keep)(const char *tag, void *ctx), void *ctx)
{
    size_t r;
    size_t w = 0;
    if (!st || !keep)
        return;
    for (r = 0; r < st->n; r++) {
        if (!keep(st->area[r].tag, ctx))
            continue;
        if (w != r)
            st->area[w] = st->area[r];
        w++;
    }
    st->n = w;
}

/* A timestamp is the same text a snapshot uses. "-" is "not set". */
static int js_time(Js *j, int64_t *out)
{
    char tmp[40];
    if (js_string(j, tmp, sizeof tmp) != 0)
        return -1;
    if (parse_unix(tmp, out) != 0)
        *out = -1;
    return 0;
}

static int js_flag(Js *j, int *out)
{
    uint32_t v = 0;
    if (js_u32(j, &v) != 0)
        return -1;
    *out = v ? 1 : 0;
    return 0;
}

/* One echo object. Unknown keys are skipped so a newer lrs can add a
 * field without making this file unreadable. The tag is required. */
static int parse_area(Js *j, State *st)
{
    int first = 1;
    StArea *a = NULL;
    char tag[LRP_TAG];
    tag[0] = '\0';
    if (js_eat(j, '{') != 0)
        return -1;
    if (js_peek(j) == '}')
        return -1;
    while (1) {
        char key[32];
        if (!first && js_eat(j, ',') != 0)
            return -1;
        first = 0;
        if (js_string(j, key, sizeof key) != 0 || js_eat(j, ':') != 0)
            return -1;
        if (strcmp(key, "tag") == 0) {
            if (js_string(j, tag, sizeof tag) != 0 || !tag[0])
                return -1;
            a = state_area(st, tag);
            if (!a)
                return -1;
        } else if (!a) {
            /* The tag names the record. A field before it has nowhere
             * to go, and guessing would attach it to the previous echo. */
            return -1;
        } else if (strcmp(key, "msgid") == 0) {
            if (js_string(j, a->msgid, sizeof a->msgid) != 0)
                return -1;
        } else if (strcmp(key, "msgdate") == 0) {
            if (js_time(j, &a->date) != 0)
                return -1;
        } else if (strcmp(key, "msgno") == 0) {
            if (js_u32(j, &a->msgno) != 0)
                return -1;
        } else if (strcmp(key, "gen") == 0) {
            if (js_u32(j, &a->gen) != 0)
                return -1;
        } else if (strcmp(key, "seen") == 0) {
            if (js_u32(j, &a->seen) != 0)
                return -1;
        } else if (strcmp(key, "peergen") == 0) {
            if (js_u32(j, &a->peergen) != 0)
                return -1;
        } else if (strcmp(key, "updated") == 0) {
            if (js_time(j, &a->updated) != 0)
                return -1;
        } else if (strcmp(key, "pending") == 0) {
            if (js_flag(j, &a->pending) != 0)
                return -1;
        } else if (strcmp(key, "pend_msgid") == 0) {
            if (js_string(j, a->pend_msgid, sizeof a->pend_msgid) != 0)
                return -1;
        } else if (strcmp(key, "pend_date") == 0) {
            if (js_time(j, &a->pend_date) != 0)
                return -1;
        } else if (strcmp(key, "pend_gen") == 0) {
            if (js_u32(j, &a->pend_gen) != 0)
                return -1;
        } else if (strcmp(key, "pend_seen") == 0) {
            if (js_u32(j, &a->pend_seen) != 0)
                return -1;
        } else if (strcmp(key, "pend_updated") == 0) {
            if (js_time(j, &a->pend_updated) != 0)
                return -1;
        } else if (strcmp(key, "pend_from") == 0) {
            if (js_string(j, a->pend_from, sizeof a->pend_from) != 0)
                return -1;
        } else if (strcmp(key, "hold") == 0) {
            if (js_flag(j, &a->hold) != 0)
                return -1;
        } else if (strcmp(key, "ask") == 0) {
            if (js_flag(j, &a->ask) != 0)
                return -1;
        } else if (strcmp(key, "asked") == 0) {
            if (js_flag(j, &a->asked) != 0)
                return -1;
        } else if (strcmp(key, "reply") == 0) {
            if (js_flag(j, &a->reply) != 0)
                return -1;
        } else if (js_skip_value(j, 0) != 0) {
            return -1;
        }
        if (js_peek(j) == '}') {
            j->p++;
            break;
        }
    }
    if (!a || !a->tag[0])
        return -1;
    if (!a->msgid[0])
        memcpy(a->msgid, "-", 2);
    if (!a->pend_msgid[0])
        memcpy(a->pend_msgid, "-", 2);
    return 0;
}

static int state_load_json(const char *text, State *st)
{
    Js j;
    int first = 1;
    int saw = 0;
    js_init(&j, text);
    if (js_eat(&j, '{') != 0)
        return -1;
    if (js_peek(&j) == '}')
        return -1;
    while (1) {
        char key[32];
        if (!first && js_eat(&j, ',') != 0)
            return -1;
        first = 0;
        if (js_string(&j, key, sizeof key) != 0 || js_eat(&j, ':') != 0)
            return -1;
        if (strcmp(key, "lrs") == 0) {
            uint32_t ver = 0;
            if (js_u32(&j, &ver) != 0 || ver != 1)
                return -1;
            saw = 1;
        } else if (strcmp(key, "sendseq") == 0) {
            if (js_u32(&j, &st->sendseq) != 0)
                return -1;
        } else if (strcmp(key, "queued") == 0) {
            if (js_string(&j, st->queued, sizeof st->queued) != 0)
                return -1;
        } else if (strcmp(key, "area") == 0) {
            int afirst = 1;
            if (js_eat(&j, '[') != 0)
                return -1;
            if (js_peek(&j) != ']') {
                while (1) {
                    if (!afirst && js_eat(&j, ',') != 0)
                        return -1;
                    afirst = 0;
                    if (parse_area(&j, st) != 0)
                        return -1;
                    if (js_peek(&j) == ']')
                        break;
                }
            }
            if (js_eat(&j, ']') != 0)
                return -1;
        } else if (js_skip_value(&j, 0) != 0) {
            return -1;
        }
        if (js_peek(&j) == '}') {
            j.p++;
            break;
        }
    }
    js_skip(&j);
    if (j.p != j.end || !saw)
        return -1;
    return 0;
}

int state_load(const char *path, State *st)
{
    char *text;
    int c;
    int rc;
    const char *p;
    memset(st, 0, sizeof *st);
    /* First sync has nothing to remember. Any other failure is a
     * damaged file: returning empty would republish every cursor. */
    if (!path || !path_exists(path))
        return 0;
    text = read_all(path, NULL);
    if (!text)
        return -1;
    p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    c = (unsigned char)*p;
    if (c != '{') {
        free(text);
        return -1;
    }
    rc = state_load_json(text, st);
    free(text);
    if (rc != 0) {
        state_free(st);
        memset(st, 0, sizeof *st);
    }
    return rc;
}

static int put_time(JBuf *b, int64_t t)
{
    char tmp[40];
    if (format_unix(t, tmp, sizeof tmp) != 0)
        snprintf(tmp, sizeof tmp, "-");
    return jbuf_json(b, tmp);
}

static int put_area(JBuf *b, const StArea *a)
{
    char num[160];
    if (jbuf_str(b, "    {\"tag\":") != 0 || jbuf_json(b, a->tag) != 0)
        return -1;
    if (jbuf_str(b, ",\"msgid\":") != 0
        || jbuf_json(b, a->msgid[0] ? a->msgid : "-") != 0
        || jbuf_str(b, ",\"msgdate\":") != 0
        || put_time(b, a->date) != 0)
        return -1;
    snprintf(num, sizeof num, ",\"msgno\":%u,\"gen\":%u,\"seen\":%u,\"peergen\":%u,\"updated\":",
        a->msgno, a->gen, a->seen, a->peergen);
    if (jbuf_str(b, num) != 0 || put_time(b, a->updated) != 0)
        return -1;
    snprintf(num, sizeof num, ",\"pending\":%d,\"pend_msgid\":", a->pending ? 1 : 0);
    if (jbuf_str(b, num) != 0
        || jbuf_json(b, a->pend_msgid[0] ? a->pend_msgid : "-") != 0
        || jbuf_str(b, ",\"pend_date\":") != 0
        || put_time(b, a->pend_date) != 0)
        return -1;
    snprintf(num, sizeof num,
        ",\"pend_gen\":%u,\"pend_seen\":%u,\"pend_updated\":",
        a->pend_gen, a->pend_seen);
    if (jbuf_str(b, num) != 0 || put_time(b, a->pend_updated) != 0)
        return -1;
    if (jbuf_str(b, ",\"pend_from\":") != 0 || jbuf_json(b, a->pend_from) != 0)
        return -1;
    snprintf(num, sizeof num, ",\"hold\":%d,\"ask\":%d,\"asked\":%d,\"reply\":%d}",
        a->hold ? 1 : 0, a->ask ? 1 : 0, a->asked ? 1 : 0, a->reply ? 1 : 0);
    return jbuf_str(b, num);
}

int state_save(const char *path, const State *st)
{
    JBuf b;
    size_t i;
    char num[32];
    char *text;
    int rc;
    if (!st || jbuf_init(&b) != 0)
        return -1;
    snprintf(num, sizeof num, "%u", st->sendseq);
    if (jbuf_str(&b, "{\n  \"lrs\": 1,\n  \"sendseq\": ") != 0
        || jbuf_str(&b, num) != 0
        || jbuf_str(&b, ",\n  \"queued\": ") != 0
        || jbuf_json(&b, st->queued) != 0
        || jbuf_str(&b, ",\n  \"area\": [\n") != 0)
        goto fail;
    for (i = 0; i < st->n; i++) {
        if (i > 0 && jbuf_str(&b, ",\n") != 0)
            goto fail;
        if (put_area(&b, &st->area[i]) != 0)
            goto fail;
    }
    if (jbuf_str(&b, "\n  ]\n}\n") != 0)
        goto fail;
    text = jbuf_take(&b);
    rc = write_atomic(path, text);
    free(text);
    return rc;
fail:
    jbuf_free(&b);
    return -1;
}
