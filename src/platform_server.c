#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "platform.h"

void rs2_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void rs2_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#ifdef USE_FLOATS
double jrand(void) {
    return (float)rand() / ((float)RAND_MAX + 1.0);
}
#else
double jrand(void) {
    return (double)rand() / ((double)RAND_MAX + 1.0);
}
#endif

int indexof(const char *str, const char *str2) {
    const char *pos = strstr(str, str2);

    if (pos) {
        return (int)(pos - str);
    }

    return -1;
}

char *substring(const char *src, size_t start, size_t length) {
    size_t len = length - start;
    char *sub = malloc(len + 1);

    strncpy(sub, src + start, len);
    sub[len] = '\0';
    return sub;
}

char *valueof(int value) {
    char *str = malloc(12 * sizeof(char));
    sprintf(str, "%d", value);
    return str;
}

bool strstartswith(const char *str, const char *prefix) {
    size_t len = strlen(prefix);
    if (len > strlen(str)) {
        return false;
    }

    return strncmp(str, prefix, len) == 0;
}

bool strendswith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return false;
    }

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

int platform_strcasecmp(const char *_l, const char *_r) {
#if defined(_WIN32) || defined(NXDK)
    return _stricmp(_l, _r);
#else
    const unsigned char *l = (void *)_l, *r = (void *)_r;
    for (; *l && *r && (*l == *r || tolower(*l) == tolower(*r)); l++, r++)
        ;
    return tolower(*l) - tolower(*r);
#endif
}

void strtolower(char *s) {
    while (*s) {
        *s = tolower((unsigned char)*s);
        s++;
    }
}

void strtoupper(char *s) {
    while (*s) {
        *s = toupper((unsigned char)*s);
        s++;
    }
}

void strtrim(char *s) {
    unsigned char *p = (unsigned char *)s;
    int l = (int)strlen(s);

    while (isspace(p[l - 1])) {
        p[--l] = 0;
    }

    while (*p && isspace(*p)) {
        ++p;
        --l;
    }

    memmove(s, p, l + 1);
}

char *platform_strdup(const char *s) {
    size_t len = strlen(s);
    char *n = malloc(len + 1);
    if (n) {
        memcpy(n, s, len);
        n[len] = '\0';
    }
    return n;
}

char *platform_strndup(const char *s, size_t len) {
    char *n = malloc(len + 1);
    if (n) {
        memcpy(n, s, len);
        n[len] = '\0';
    }
    return n;
}
