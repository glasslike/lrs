#include "json.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void js_init(Js *j, const char *text)
{
    j->p = text ? text : "";
    j->end = j->p + strlen(j->p);
}

void js_skip(Js *j)
{
    while (j->p < j->end) {
        char c = *j->p;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        j->p++;
    }
}

int js_peek(Js *j)
{
    js_skip(j);
    if (j->p >= j->end)
        return -1;
    return (unsigned char)*j->p;
}

int js_eat(Js *j, char c)
{
    if (js_peek(j) != (unsigned char)c)
        return -1;
    j->p++;
    return 0;
}

static int js_hex4(Js *j, unsigned *v)
{
    int k;
    *v = 0;
    for (k = 0; k < 4; k++) {
        unsigned char h;
        if (j->p >= j->end)
            return -1;
        h = (unsigned char)*j->p++;
        *v <<= 4;
        if (h >= '0' && h <= '9')
            *v |= (unsigned)(h - '0');
        else if (h >= 'a' && h <= 'f')
            *v |= (unsigned)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F')
            *v |= (unsigned)(h - 'A' + 10);
        else
            return -1;
    }
    return 0;
}

int js_string(Js *j, char *dst, size_t cap)
{
    size_t n = 0;
    if (js_eat(j, '"') != 0)
        return -1;
    while (j->p < j->end) {
        unsigned char c = (unsigned char)*j->p++;
        if (c == '"') {
            if (n >= cap)
                return -1;
            dst[n] = '\0';
            return 0;
        }
        if (c == '\\') {
            unsigned u;
            if (j->p >= j->end)
                return -1;
            c = (unsigned char)*j->p++;
            switch (c) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u':
                if (js_hex4(j, &u) != 0 || u > 255)
                    return -1;
                c = (unsigned char)u;
                break;
            default:
                return -1;
            }
        } else if (c < 0x20) {
            return -1;
        }
        if (n + 1 >= cap)
            return -1;
        dst[n++] = (char)c;
    }
    return -1;
}

static int js_skip_string(Js *j)
{
    if (js_eat(j, '"') != 0)
        return -1;
    while (j->p < j->end) {
        unsigned char c = (unsigned char)*j->p++;
        if (c == '"')
            return 0;
        if (c == '\\') {
            if (j->p >= j->end)
                return -1;
            if (*j->p == 'u') {
                unsigned u;
                j->p++;
                if (js_hex4(j, &u) != 0)
                    return -1;
            } else {
                j->p++;
            }
        }
    }
    return -1;
}

static int js_skip_object(Js *j, int depth);

static int js_skip_array(Js *j, int depth)
{
    int first = 1;
    if (js_eat(j, '[') != 0)
        return -1;
    if (js_peek(j) == ']') {
        j->p++;
        return 0;
    }
    while (1) {
        if (!first && js_eat(j, ',') != 0)
            return -1;
        first = 0;
        if (js_skip_value(j, depth + 1) != 0)
            return -1;
        if (js_peek(j) == ']') {
            j->p++;
            return 0;
        }
    }
}

static int js_skip_object(Js *j, int depth)
{
    int first = 1;
    if (js_eat(j, '{') != 0)
        return -1;
    if (js_peek(j) == '}') {
        j->p++;
        return 0;
    }
    while (1) {
        if (!first && js_eat(j, ',') != 0)
            return -1;
        first = 0;
        if (js_skip_string(j) != 0 || js_eat(j, ':') != 0)
            return -1;
        if (js_skip_value(j, depth + 1) != 0)
            return -1;
        if (js_peek(j) == '}') {
            j->p++;
            return 0;
        }
    }
}

int js_skip_value(Js *j, int depth)
{
    int c;
    if (depth > 8)
        return -1;
    c = js_peek(j);
    if (c < 0)
        return -1;
    if (c == '"')
        return js_skip_string(j);
    if (c == '{')
        return js_skip_object(j, depth);
    if (c == '[')
        return js_skip_array(j, depth);
    if (c == 't') {
        if (j->end - j->p < 4 || memcmp(j->p, "true", 4) != 0)
            return -1;
        j->p += 4;
        return 0;
    }
    if (c == 'f') {
        if (j->end - j->p < 5 || memcmp(j->p, "false", 5) != 0)
            return -1;
        j->p += 5;
        return 0;
    }
    if (c == 'n') {
        if (j->end - j->p < 4 || memcmp(j->p, "null", 4) != 0)
            return -1;
        j->p += 4;
        return 0;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        if (c == '-')
            j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9')
            return -1;
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
        if (j->p < j->end && *j->p == '.') {
            j->p++;
            if (j->p >= j->end || *j->p < '0' || *j->p > '9')
                return -1;
            while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
                j->p++;
        }
        if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
            j->p++;
            if (j->p < j->end && (*j->p == '+' || *j->p == '-'))
                j->p++;
            if (j->p >= j->end || *j->p < '0' || *j->p > '9')
                return -1;
            while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
                j->p++;
        }
        return 0;
    }
    return -1;
}

int js_u32(Js *j, uint32_t *out)
{
    const char *s;
    char *end;
    unsigned long long v;
    js_skip(j);
    if (j->p >= j->end || *j->p < '0' || *j->p > '9')
        return -1;
    if (*j->p == '0' && j->p + 1 < j->end && j->p[1] >= '0' && j->p[1] <= '9')
        return -1;
    s = j->p;
    /* unsigned long is 32 bits on Windows, so strtoul cannot tell a
     * value above 2^32-1 from that maximum. strtoull can. */
    errno = 0;
    v = strtoull(s, &end, 10);
    if (end == s || (size_t)(end - s) > 10 || errno == ERANGE || v > 0xffffffffull)
        return -1;
    j->p = end;
    *out = (uint32_t)v;
    return 0;
}

int jbuf_init(JBuf *b)
{
    b->data = (char *)malloc(256);
    if (!b->data)
        return -1;
    b->data[0] = '\0';
    b->used = 0;
    b->cap = 256;
    return 0;
}

void jbuf_free(JBuf *b)
{
    if (!b)
        return;
    free(b->data);
    b->data = NULL;
    b->used = 0;
    b->cap = 0;
}

static int jbuf_add(JBuf *b, const char *s, size_t n)
{
    if (b->used + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap : 256;
        char *grown;
        while (b->used + n + 1 > ncap)
            ncap *= 2;
        grown = (char *)realloc(b->data, ncap);
        if (!grown)
            return -1;
        b->data = grown;
        b->cap = ncap;
    }
    memcpy(b->data + b->used, s, n);
    b->used += n;
    b->data[b->used] = '\0';
    return 0;
}

int jbuf_str(JBuf *b, const char *s)
{
    if (!s)
        s = "";
    return jbuf_add(b, s, strlen(s));
}

int jbuf_json(JBuf *b, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    if (jbuf_str(b, "\"") != 0)
        return -1;
    if (!s)
        s = "";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            if (jbuf_add(b, esc, 2) != 0)
                return -1;
        } else if (c >= 0x20 && c < 0x7f) {
            if (jbuf_add(b, (const char *)&c, 1) != 0)
                return -1;
        } else if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') {
            char shortesc[2];
            shortesc[0] = '\\';
            if (c == '\b') shortesc[1] = 'b';
            else if (c == '\f') shortesc[1] = 'f';
            else if (c == '\n') shortesc[1] = 'n';
            else if (c == '\r') shortesc[1] = 'r';
            else shortesc[1] = 't';
            if (jbuf_add(b, shortesc, 2) != 0)
                return -1;
        } else {
            esc[0] = '\\';
            esc[1] = 'u';
            esc[2] = '0';
            esc[3] = '0';
            esc[4] = hex[c >> 4];
            esc[5] = hex[c & 0x0f];
            if (jbuf_add(b, esc, 6) != 0)
                return -1;
        }
    }
    return jbuf_str(b, "\"");
}

char *jbuf_take(JBuf *b)
{
    char *p = b->data;
    b->data = NULL;
    b->used = 0;
    b->cap = 0;
    return p;
}
