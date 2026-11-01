#ifndef LRP_JSON_H
#define LRP_JSON_H

/*
 * The small JSON subset used by a snapshot and by lrs.state.
 *
 * Strings are bytes, not Unicode text. Printable ASCII is written as
 * itself. A quote or a backslash is escaped. The short escapes cover
 * the usual controls, and every other byte is \u00XX. On the way back
 * \u00XX is one byte; a code point above 255 is rejected, so a MSGID
 * cannot be widened or truncated. Both files share this so a value
 * stored locally is the same bytes the peer will see.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct Js {
    const char *p;
    const char *end;
} Js;

void js_init(Js *j, const char *text);
int js_peek(Js *j);
int js_eat(Js *j, char c);
void js_skip(Js *j);
/* 0, and the decoded bytes in dst. Fails if they do not fit. */
int js_string(Js *j, char *dst, size_t cap);
/* Unsigned decimal, no leading zero. Values above 2^32-1 fail. */
int js_u32(Js *j, uint32_t *out);
/* Drop one value. depth counts nested objects and arrays. */
int js_skip_value(Js *j, int depth);

/* A growing text. jbuf_take hands the bytes to the caller and leaves
 * the buffer empty. jbuf_json writes one quoted string. */
typedef struct JBuf {
    char *data;
    size_t used;
    size_t cap;
} JBuf;

int jbuf_init(JBuf *b);
void jbuf_free(JBuf *b);
int jbuf_str(JBuf *b, const char *s);
int jbuf_json(JBuf *b, const char *s);
char *jbuf_take(JBuf *b);

#endif
