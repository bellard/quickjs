/*
 * Javascript Compressor
 * 
 * Copyright (c) 2008-2018 Fabrice Bellard
 * Copyright (c) 2017-2018 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include "cutils.h"

typedef struct JSToken {
    int tok;
    char buf[20];
    char *str;
    int len;
    int size;
    int line_num;  /* line number for start of token */
    int lines;     /* number of embedded linefeeds in token */
} JSToken;

enum {
    TOK_EOF = 256,
    TOK_IDENT,
    TOK_STR1,
    TOK_STR2,
    TOK_STR3,
    TOK_NUM,
    TOK_COM,
    TOK_LCOM,
};

void tok_reset(JSToken *tt)
{
    if (tt->str != tt->buf) {
        free(tt->str);
        tt->str = tt->buf;
        tt->size = sizeof(tt->buf);
    }
    tt->len = 0;
}

void tok_add_ch(JSToken *tt, int c)
{
    if (tt->len + 1 > tt->size) {
        tt->size *= 2;
        if (tt->str == tt->buf) {
            tt->str = malloc(tt->size);
            memcpy(tt->str, tt->buf, tt->len);
        } else {
            tt->str = realloc(tt->str, tt->size);
        }
    }
    tt->str[tt->len++] = c;
}

FILE *infile;
const char *filename;
int output_line_num;
int line_num;
int ch;
JSToken tokc;

int skip_mask;
#define DEFINE_MAX 20
char *define_tab[DEFINE_MAX];
int define_len;

void error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (filename) {
        fprintf(stderr, "%s:%d: ", filename, line_num);
    } else {
        fprintf(stderr, "jscompress: ");
    }
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void define_symbol(const char *def)
{
    int i;
    for (i = 0; i < define_len; i++) {
        if (!strcmp(tokc.str, define_tab[i]))
            return;
    }
    if (define_len >= DEFINE_MAX)
        error("too many defines");
    define_tab[define_len++] = strdup(def);
}

void undefine_symbol(const char *def)
{
    int i, j;
    for (i = j = 0; i < define_len; i++) {
        if (!strcmp(tokc.str, define_tab[i])) {
            free(define_tab[i]);
        } else {
            define_tab[j++] = define_tab[i];
        }
    }
    define_len = j;
}

const char *find_symbol(const char *def)
{
    int i;
    for (i = 0; i < define_len; i++) {
        if (!strcmp(tokc.str, define_tab[i]))
            return "1";
    }
    return NULL;
}

void next(void);

void nextch(void)
{
    ch = fgetc(infile);
    if (ch == '\n')
        line_num++;
}

int skip_blanks(void)
{
    for (;;) {
        next();
        if (tokc.tok != ' ' && tokc.tok != '\t' &&
            tokc.tok != TOK_COM && tokc.tok != TOK_LCOM)
            return tokc.tok;
    }
}

void parse_directive(void)
{
    int ifdef, mask = skip_mask;
    /* simplistic preprocessor:
       #define / #undef / #ifdef / #ifndef / #else / #endif
       no symbol substitution.
     */
    skip_mask = 0;  /* disable skipping to parse preprocessor line */
    nextch();
    if (skip_blanks() != TOK_IDENT)
        error("expected preprocessing directive after #");

    if (!strcmp(tokc.str, "define")) {
        if (skip_blanks() != TOK_IDENT)
            error("expected identifier after #define");
        define_symbol(tokc.str);
    } else if (!strcmp(tokc.str, "undef")) {
        if (skip_blanks() != TOK_IDENT)
            error("expected identifier after #undef");
        undefine_symbol(tokc.str);
    } else if ((ifdef = 1, !strcmp(tokc.str, "ifdef")) ||
               (ifdef = 0, !strcmp(tokc.str, "ifndef"))) {
        if (skip_blanks() != TOK_IDENT)
            error("expected identifier after #ifdef/#ifndef");
        mask = (mask << 2) | 2 | ifdef;
        if (find_symbol(tokc.str))
            mask ^= 1;
    } else if (!strcmp(tokc.str, "else")) {
        if (!(mask & 2))
            error("#else without a #if");
        mask ^= 1;
    } else if (!strcmp(tokc.str, "endif")) {
        if (!(mask & 2))
            error("#endif without a #if");
        mask >>= 2;
    } else {
        error("unsupported preprocessing directive");
    }
    if (skip_blanks() != '\n')
        error("extra characters on preprocessing line");
    skip_mask = mask;
}

/* return -1 if invalid char */
static int hex_to_num(int ch)
{
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    else if (ch >= '0' && ch <= '9')
        return ch - '0';
    else
        return -1;
}

void next(void)
{
again:    
    tok_reset(&tokc);
    tokc.line_num = line_num;
    tokc.lines = 0;
    switch(ch) {
    case EOF:
        tokc.tok = TOK_EOF;
        if (skip_mask)
            error("missing #endif");
        break;
    case 'a' ... 'z':
    case 'A' ... 'Z':
    case '_':
    case '$':
        tok_add_ch(&tokc, ch);
        nextch();
        while ((ch >= 'a' && ch <= 'z') ||
               (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') ||
               (ch == '_' || ch == '$')) {
            tok_add_ch(&tokc, ch);
            nextch();
        }
        tok_add_ch(&tokc, '\0');
        tokc.tok = TOK_IDENT;
        break;
    case '.':
        nextch();
        if (ch >= '0' && ch <= '9') {
            tok_add_ch(&tokc, '.');
            goto has_dot;
        }
        tokc.tok = '.';
        break;
    case '0':
        tok_add_ch(&tokc, ch);
        nextch();
        if (ch == 'x' || ch == 'X') {
            /* hexa */
            tok_add_ch(&tokc, ch);
            nextch();
            while ((ch >= 'a' && ch <= 'f') ||
                   (ch >= 'A' && ch <= 'F') ||
                   (ch >= '0' && ch <= '9')) {
                tok_add_ch(&tokc, ch);
                nextch();
            }
            tok_add_ch(&tokc, '\0');
            tokc.tok = TOK_NUM;
            break;
        }
        goto has_digit;

    case '1' ... '9':
        tok_add_ch(&tokc, ch);
        nextch();
    has_digit:
        /* decimal */
        while (ch >= '0' && ch <= '9') {
            tok_add_ch(&tokc, ch);
            nextch();
        }
        if (ch == '.') {
            tok_add_ch(&tokc, ch);
            nextch();
        has_dot:
            while (ch >= '0' && ch <= '9') {
                tok_add_ch(&tokc, ch);
                nextch();
            }
        }
        if (ch == 'e' || ch == 'E') {
            tok_add_ch(&tokc, ch);
            nextch();
            if (ch == '+' || ch == '-') {
                tok_add_ch(&tokc, ch);
                nextch();
            }
            while (ch >= '0' && ch <= '9') {
                tok_add_ch(&tokc, ch);
                nextch();
            }
        }
        tok_add_ch(&tokc, '\0');
        tokc.tok = TOK_NUM;
        break;
    case '`':
        {
            nextch();
            while (ch != '`' && ch != EOF) {
                if (ch == '\\') {
                    tok_add_ch(&tokc, ch);
                    nextch();
                    if (ch == EOF) {
                        error("unexpected char after '\\'");
                    }
                    tok_add_ch(&tokc, ch);
                } else {
                    tok_add_ch(&tokc, ch);
                    nextch();
                }
            }
            nextch();
            tok_add_ch(&tokc, 0);
            tokc.tok = TOK_STR3;
        }
        break;
    case '\"':
    case '\'':
        {
            int n, i, c, hex_digit_count;
            int quote_ch;
            quote_ch = ch;
            nextch();
            while (ch != quote_ch && ch != EOF) {
                if (ch == '\\') {
                    nextch();
                    switch(ch) {
                    case 'n':
                        tok_add_ch(&tokc, '\n');
                        nextch();
                        break;
                    case 'r':
                        tok_add_ch(&tokc, '\r');
                        nextch();
                        break;
                    case 't':
                        tok_add_ch(&tokc, '\t');
                        nextch();
                        break;
                    case 'v':
                        tok_add_ch(&tokc, '\v');
                        nextch();
                        break;
                    case '\"':
                    case '\'':
                    case '\\':
                        tok_add_ch(&tokc, ch);
                        nextch();
                        break;
                    case '0' ... '7':
                        n = 0;
                        while (ch >= '0' && ch <= '7') {
                            n = n * 8 + (ch - '0');
                            nextch();
                        }
                        tok_add_ch(&tokc, n);
                        break;
                    case 'x':
                    case 'u':
                        if (ch == 'x')
                            hex_digit_count = 2;
                        else
                            hex_digit_count = 4;
                        nextch();
                        n = 0;
                        for(i = 0; i < hex_digit_count; i++) {
                            c = hex_to_num(ch);
                            if (c < 0) 
                                error("unexpected char after '\\x'");
                            n = n * 16 + c;
                            nextch();
                        }
                        if (n >= 256)
                            error("unicode is currently unsupported");
                        tok_add_ch(&tokc, n);
                        break;

                    default:
                        error("unexpected char after '\\'");
                    }
                } else {
                    /* XXX: should refuse embedded newlines */
                    tok_add_ch(&tokc, ch);
                    nextch();
                }
            }
            nextch();
            tok_add_ch(&tokc, 0);
            if (quote_ch == '\'')
                tokc.tok = TOK_STR1;
            else
                tokc.tok = TOK_STR2;
        }
        break;
    case '/':
        nextch();
        if (ch == '/') {
            tok_add_ch(&tokc, '/');
            tok_add_ch(&tokc, ch);
            nextch();
            while (ch != '\n' && ch != EOF) {
                tok_add_ch(&tokc, ch);
                nextch();
            }
            tok_add_ch(&tokc, '\0');
            tokc.tok = TOK_LCOM;
        } else if (ch == '*') {
            int last;
            tok_add_ch(&tokc, '/');
            tok_add_ch(&tokc, ch);
            last = 0;
            for(;;) {
                nextch();
                if (ch == EOF)
                    error("unterminated comment");
                if (ch == '\n')
                    tokc.lines++;
                tok_add_ch(&tokc, ch);
                if (last == '*' && ch == '/')
                    break;
                last = ch;
            }
            nextch();
            tok_add_ch(&tokc, '\0');
            tokc.tok = TOK_COM;
        } else {
            tokc.tok = '/';
        }
        break;
    case '#':
        parse_directive();
        goto again;
    case '\n':
        /* adjust line number */
        tokc.line_num--;
        tokc.lines++;
        /* fall thru */
    default:
        tokc.tok = ch;
        nextch();
        break;
    }
    if (skip_mask & 1)
        goto again;
}

void print_tok(FILE *f, JSToken *tt)
{
    /* keep output lines in sync with input lines */
    while (output_line_num < tt->line_num) {
        putc('\n', f);
        output_line_num++;
    }

    switch(tt->tok) {
    case TOK_IDENT:
    case TOK_COM:
    case TOK_LCOM:
        fprintf(f, "%s", tt->str);
        break;
    case TOK_NUM:
        {
            unsigned long a;
            char *p;
            a = strtoul(tt->str, &p, 0);
            if (*p == '\0' && a <= 0x7fffffff) {
                /* must be an integer */
                fprintf(f, "%d", (int)a);
            } else {
                fprintf(f, "%s", tt->str);
            }
        }
        break;
    case TOK_STR3:
        fprintf(f, "`%s`", tt->str);
        break;
    case TOK_STR1:
    case TOK_STR2:
        {
            int i, c, quote_ch;
            if (tt->tok == TOK_STR1)
                quote_ch = '\'';
            else
                quote_ch = '\"';
            fprintf(f, "%c", quote_ch);
            for(i = 0; i < tt->len - 1; i++) {
                c = (uint8_t)tt->str[i];
                switch(c) {
                case '\r':
                    fprintf(f, "\\r");
                    break;
                case '\n':
                    fprintf(f, "\\n");
                    break;
                case '\t':
                    fprintf(f, "\\t");
                    break;
                case '\v':
                    fprintf(f, "\\v");
                    break;
                case '\"':
                case '\'':
                    if (c == quote_ch)
                        fprintf(f, "\\%c", c);
                    else
                        fprintf(f, "%c", c);
                    break;
                case '\\':
                    fprintf(f, "\\\\");
                    break;
                default:
                    /* XXX: no utf-8 support! */
                    if (c >= 32 && c <= 255) {
                        fprintf(f, "%c", c);
                    } else if (c <= 255) 
                        fprintf(f, "\\x%02x", c);
                    else
                        fprintf(f, "\\u%04x", c);
                    break;
                }
            }
            fprintf(f, "%c", quote_ch);
        }
        break;
    default:
        if (tokc.tok >= 256)
            error("unsupported token in print_tok: %d", tt->tok);
        fprintf(f, "%c", tt->tok);
        break;
    }
    output_line_num += tt->lines;
}

/* check if token pasting could occur */                    
static BOOL compat_token(int c1, int c2)
{
    if ((c1 == TOK_IDENT || c1 == TOK_NUM) &&
        (c2 == TOK_IDENT || c2 == TOK_NUM))
        return FALSE;

    if ((c1 == c2 && strchr("+-<>&|=*/.", c1))
    ||  (c2 == '=' && strchr("+-<>&|!*/^%", c1))
    ||  (c1 == '=' && c2 == '>')
    ||  (c1 == '/' && c2 == '*')
    ||  (c1 == '.' && c2 == TOK_NUM)
    ||  (c1 == TOK_NUM && c2 == '.'))
        return FALSE;

    return TRUE;
}

void js_compress(const char *filename, const char *outfilename,
                 BOOL do_strip, BOOL keep_header)
{
    FILE *outfile;
    int ltok, seen_space;
    
    line_num = 1;
    infile = fopen(filename, "rb");
    if (!infile) {
        perror(filename);
        exit(1);
    }
    
    output_line_num = 1;
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        perror(outfilename);
        exit(1);
    }
        
    nextch();
    next();
    ltok = 0;
    seen_space = 0;
    if (do_strip) {
        if (keep_header) {
            while (tokc.tok == ' ' ||
                   tokc.tok == '\n' ||
                   tokc.tok == '\t' ||
                   tokc.tok == '\v' ||
                   tokc.tok == '\b' ||
                   tokc.tok == '\f') {
                seen_space = 1;
                next();
            }
            if (tokc.tok == TOK_COM) {
                print_tok(outfile, &tokc);
                //fprintf(outfile, "\n");
                ltok = tokc.tok;
                seen_space = 0;
                next();
            }
        }

        for(;;) {
            if (tokc.tok == TOK_EOF)
                break;
            if (tokc.tok == ' ' ||
                tokc.tok == '\r' ||
                tokc.tok == '\t' ||
                tokc.tok == '\v' ||
                tokc.tok == '\b' ||
                tokc.tok == '\f' ||
                tokc.tok == TOK_LCOM ||
                tokc.tok == TOK_COM) {
                /* don't print spaces or comments */
                seen_space = 1;
            } else if (tokc.tok == TOK_STR3) {
                print_tok(outfile, &tokc);
                ltok = tokc.tok;
                seen_space = 0;
            } else if (tokc.tok == TOK_STR1 || tokc.tok == TOK_STR2) {
                int count, i;
                /* find the optimal quote char */
                count = 0;
                for(i = 0; i < tokc.len; i++) {
                    if (tokc.str[i] == '\'')
                        count++;
                    else if (tokc.str[i] == '\"')
                        count--;
                }
                if (count > 0)
                    tokc.tok = TOK_STR2;
                else if (count < 0)
                    tokc.tok = TOK_STR1;
                print_tok(outfile, &tokc);
                ltok = tokc.tok;
                seen_space = 0;
            } else {
                if (seen_space && !compat_token(ltok, tokc.tok)) {
                    fprintf(outfile, " ");
                }
                print_tok(outfile, &tokc);
                ltok = tokc.tok;
                seen_space = 0;
            }
            next();
        }
    } else {
        /* just handle preprocessing */
        while (tokc.tok != TOK_EOF) {
            print_tok(outfile, &tokc);
            next();
        }
    }

    fclose(outfile);
    fclose(infile);
}

#define HASH_SIZE 30011
#define MATCH_LEN_MIN 3
#define MATCH_LEN_MAX (4 + 63)
#define DIST_MAX 65535

static int find_longest_match(int *pdist, const uint8_t *src, int src_len,
                              const int *hash_next, int cur_pos)
{
    int pos, i, match_len, match_pos, pos_min, len_max;

    len_max = min_int(src_len - cur_pos, MATCH_LEN_MAX);
    match_len = 0;
    match_pos = 0;
    pos_min = max_int(cur_pos - DIST_MAX - 1, 0);
    pos = hash_next[cur_pos];
    while (pos >= pos_min) {
        for(i = 0; i < len_max; i++) {
            if (src[cur_pos + i] != src[pos + i])
                break;
        }
        if (i > match_len) {
            match_len = i;
            match_pos = pos;
        }
        pos = hash_next[pos];
    }
    *pdist = cur_pos - match_pos - 1;
    return match_len;
}

int lz_compress(uint8_t **pdst, const uint8_t *src, int src_len)
{
    int *hash_table, *hash_next;
    uint32_t h, v;
    int i, dist, len, len1, dist1;
    uint8_t *dst, *q;
    
    /* build the hash table */
    
    hash_table = malloc(sizeof(hash_table[0]) * HASH_SIZE);
    for(i = 0; i < HASH_SIZE; i++)
        hash_table[i] = -1;
    hash_next = malloc(sizeof(hash_next[0]) * src_len);
    for(i = 0; i < src_len; i++)
        hash_next[i] = -1;

    for(i = 0; i < src_len - MATCH_LEN_MIN + 1; i++) {
        h = ((src[i] << 16) | (src[i + 1] << 8) | src[i + 2]) % HASH_SIZE;
        hash_next[i] = hash_table[h];
        hash_table[h] = i;
    }
    for(;i < src_len; i++) {
        hash_next[i] = -1;
    }
    free(hash_table);

    dst = malloc(src_len + 4); /* never larger than the source */
    q = dst;
    *q++ = src_len >> 24;
    *q++ = src_len >> 16;
    *q++ = src_len >> 8;
    *q++ = src_len >> 0;
    /* compress */
    i = 0;
    while (i < src_len) {
        if (src[i] >= 128)
            return -1;
        len = find_longest_match(&dist, src, src_len, hash_next, i);
        if (len >= MATCH_LEN_MIN) {
            /* heuristic: see if better length just after */
            len1 = find_longest_match(&dist1, src, src_len, hash_next, i + 1);
            if (len1 > len)
                goto no_match;
        }
        if (len < MATCH_LEN_MIN) {
        no_match:
            *q++ = src[i];
            i++;
        } else if (len <= (3 + 15) && dist < (1 << 10)) {
            v = 0x8000 | ((len - 3) << 10) | dist;
            *q++ = v >> 8;
            *q++ = v;
            i += len;
        } else if (len >= 4 && len <= (4 + 63) && dist < (1 << 16)) {
            v = 0xc00000 | ((len - 4) << 16) | dist;
            *q++ = v >> 16;
            *q++ = v >> 8;
            *q++ = v;
            i += len;
        } else {
            goto no_match;
        }
    }
    free(hash_next);
    *pdst = dst;
    return q - dst;
}

static int load_file(uint8_t **pbuf, const char *filename)
{
    FILE *f;
    uint8_t *buf;
    int buf_len;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(buf_len + 1);
    fread(buf, 1, buf_len, f);
    buf[buf_len] = '\0';
    fclose(f);
    *pbuf = buf;
    return buf_len;
}

static void save_file(const char *filename, const uint8_t *buf, int buf_len)
{
    FILE *f;
    
    f = fopen(filename, "wb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fwrite(buf, 1, buf_len, f);
    fclose(f);
}

static void save_c_source(const char *filename, const uint8_t *buf, int buf_len,
                          const char *var_name)
{
    FILE *f;
    int i;
    
    f = fopen(filename, "wb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fprintf(f, "/* This file is automatically generated - do not edit */\n\n");
    fprintf(f, "const uint8_t %s[] = {\n", var_name);
    for(i = 0; i < buf_len; i++) {
        fprintf(f, " 0x%02x,", buf[i]);
        if ((i % 8) == 7 || (i == buf_len - 1))
            fprintf(f, "\n");
    }
    fprintf(f, "};\n");
    fclose(f);
}

#define DEFAULT_OUTPUT_FILENAME "out.js"

void help(void)
{
    printf("jscompress version 1.0 Copyright (c) 2008-2018 Fabrice Bellard\n"
           "usage: jscompress [options] filename\n"
           "Javascript compressor\n"
           "\n"
           "-h          print this help\n"
           "-n          do not compress spaces\n"
           "-H          keep the first comment\n"
           "-c          compress to file\n"
           "-C name     compress to C source ('name' is the variable name)\n"
           "-D symbol   define preprocessor symbol\n"
           "-U symbol   undefine preprocessor symbol\n"
           "-o outfile  set the output filename (default=%s)\n",
           DEFAULT_OUTPUT_FILENAME);
    exit(1);
}

int main(int argc, char **argv)
{
    int c, do_strip, keep_header, compress;
    const char *out_filename, *c_var, *fname;
    char tmpfilename[1024];

    do_strip = 1;
    keep_header = 0;
    out_filename = DEFAULT_OUTPUT_FILENAME;
    compress = 0;
    c_var = NULL;
    for(;;) {
        c = getopt(argc, argv, "hno:HcC:D:U:");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        case 'n':
            do_strip = 0;
            break;
        case 'o':
            out_filename = optarg;
            break;
        case 'H':
            keep_header = 1;
            break;
        case 'c':
            compress = 1;
            break;
        case 'C':
            c_var = optarg;
            compress = 1;
            break;
        case 'D':
            define_symbol(optarg);
            break;
        case 'U':
            undefine_symbol(optarg);
            break;
        }
    }
    if (optind >= argc)
        help();

    filename = argv[optind++];

    if (compress) {
#if defined(__ANDROID__)
        /* XXX: use another directory ? */
        snprintf(tmpfilename, sizeof(tmpfilename), "out.%d", getpid());
#else
        snprintf(tmpfilename, sizeof(tmpfilename), "/tmp/out.%d", getpid());
#endif
        fname = tmpfilename;
    } else {
        fname = out_filename;
    }
    js_compress(filename, fname, do_strip, keep_header);
    
    if (compress) {
        uint8_t *buf1, *buf2; 
        int buf1_len, buf2_len;
        
        buf1_len = load_file(&buf1, fname);
        unlink(fname);
        buf2_len = lz_compress(&buf2, buf1, buf1_len);
        if (buf2_len < 0) {
            fprintf(stderr, "Could not compress file (UTF8 chars are forbidden)\n");
            exit(1);
        }
        
        if (c_var) {
            save_c_source(out_filename, buf2, buf2_len, c_var);
        } else {
            save_file(out_filename, buf2, buf2_len);
        }
        free(buf1);
        free(buf2);
    }
    return 0;
}
