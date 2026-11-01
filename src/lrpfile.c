#include "lrpfile.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lrp_file_free(LrpFile *f)
{
    if (!f)
        return;
    free(f->area);
    f->area = NULL;
    f->n = 0;
    f->from[0] = '\0';
}

int lrp_file_add(LrpFile *f, const LrpArea *area)
{
    LrpArea *grown;
    grown = (LrpArea *)realloc(f->area, (f->n + 1) * sizeof(LrpArea));
    if (!grown)
        return -1;
    f->area = grown;
    f->area[f->n++] = *area;
    return 0;
}


static int js_echo_object(Js *j, LrpArea *a)
{
    int first = 1;
    memset(a, 0, sizeof *a);
    a->date = -1;
    a->updated = -1;
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
            if (js_string(j, a->tag, sizeof a->tag) != 0 || !a->tag[0])
                return -1;
        } else if (strcmp(key, "msgid") == 0) {
            if (js_string(j, a->msgid, sizeof a->msgid) != 0)
                return -1;
        } else if (strcmp(key, "date") == 0 || strcmp(key, "updated") == 0) {
            char tmp[40];
            int64_t t = -1;
            if (js_string(j, tmp, sizeof tmp) != 0)
                return -1;
            if (parse_unix(tmp, &t) != 0)
                t = -1;
            if (strcmp(key, "date") == 0)
                a->date = t;
            else
                a->updated = t;
        } else if (strcmp(key, "gen") == 0) {
            if (js_u32(j, &a->gen) != 0)
                return -1;
        } else if (strcmp(key, "seen") == 0) {
            if (js_u32(j, &a->seen) != 0)
                return -1;
        } else if (strcmp(key, "request") == 0) {
            uint32_t r = 0;
            if (js_u32(j, &r) != 0)
                return -1;
            a->request = r ? 1 : 0;
        } else if (js_skip_value(j, 0) != 0) {
            return -1;
        }
        if (js_peek(j) == '}') {
            j->p++;
            break;
        }
    }
    if (!a->tag[0])
        return -1;
    if (!a->msgid[0])
        memcpy(a->msgid, "-", 2);
    return 0;
}

static int parse_json(const char *text, LrpFile *out)
{
    Js j;
    int first = 1;
    int saw_lrp = 0;
    memset(out, 0, sizeof *out);
    js_init(&j, text);
    if (js_eat(&j, '{') != 0)
        return -1;
    if (js_peek(&j) == '}')
        goto fail;
    while (1) {
        char key[32];
        if (!first && js_eat(&j, ',') != 0)
            goto fail;
        first = 0;
        if (js_string(&j, key, sizeof key) != 0 || js_eat(&j, ':') != 0)
            goto fail;
        if (strcmp(key, "lrp") == 0) {
            uint32_t ver = 0;
            if (js_u32(&j, &ver) != 0 || ver != 2)
                goto fail;
            saw_lrp = 1;
        } else if (strcmp(key, "from") == 0) {
            if (js_string(&j, out->from, sizeof out->from) != 0 || !out->from[0])
                goto fail;
        } else if (strcmp(key, "echo") == 0) {
            int afirst = 1;
            if (js_eat(&j, '[') != 0)
                goto fail;
            if (js_peek(&j) != ']') {
                while (1) {
                    LrpArea a;
                    if (!afirst && js_eat(&j, ',') != 0)
                        goto fail;
                    afirst = 0;
                    if (js_echo_object(&j, &a) != 0 || lrp_file_add(out, &a) != 0)
                        goto fail;
                    if (js_peek(&j) == ']')
                        break;
                }
            }
            if (js_eat(&j, ']') != 0)
                goto fail;
        } else if (js_skip_value(&j, 0) != 0) {
            goto fail;
        }
        if (js_peek(&j) == '}') {
            j.p++;
            break;
        }
    }
    js_skip(&j);
    if (j.p != j.end || !saw_lrp || !out->from[0])
        goto fail;
    return 0;
fail:
    lrp_file_free(out);
    return -1;
}

int lrp_file_parse(const char *text, LrpFile *out)
{
    const char *p;
    if (!text || !out)
        return -1;
    p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '{')
        return -1;
    return parse_json(text, out);
}


char *lrp_file_format(const LrpFile *f)
{
    JBuf b;
    size_t i;
    char num[96];
    if (!f || jbuf_init(&b) != 0)
        return NULL;
    if (jbuf_str(&b, "{\n  \"lrp\": 2,\n  \"from\": ") != 0
        || jbuf_json(&b, f->from) != 0
        || jbuf_str(&b, ",\n  \"echo\": [\n") != 0)
        goto fail;
    for (i = 0; i < f->n; i++) {
        const LrpArea *a = &f->area[i];
        char dated[40];
        char when[40];
        if (i > 0 && jbuf_str(&b, ",\n") != 0)
            goto fail;
        if (format_unix(a->date, dated, sizeof dated) != 0)
            strcpy(dated, "-");
        if (format_unix(a->updated, when, sizeof when) != 0)
            strcpy(when, "-");
        if (jbuf_str(&b, "    {\"tag\":") != 0
            || jbuf_json(&b, a->tag) != 0)
            goto fail;
        snprintf(num, sizeof num, ",\"gen\":%u,\"seen\":%u,\"date\":", a->gen, a->seen);
        if (jbuf_str(&b, num) != 0
            || jbuf_json(&b, dated) != 0
            || jbuf_str(&b, ",\"updated\":") != 0
            || jbuf_json(&b, when) != 0)
            goto fail;
        snprintf(num, sizeof num, ",\"request\":%d,\"msgid\":", a->request ? 1 : 0);
        if (jbuf_str(&b, num) != 0
            || jbuf_json(&b, a->msgid[0] ? a->msgid : "-") != 0
            || jbuf_str(&b, "}") != 0)
            goto fail;
    }
    if (jbuf_str(&b, "\n  ]\n}\n") != 0)
        goto fail;
    return jbuf_take(&b);
fail:
    jbuf_free(&b);
    return NULL;
}
