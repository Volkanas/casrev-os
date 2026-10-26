#ifndef CASREV_STRING_H
#define CASREV_STRING_H

#include "types.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
size_t strlen(const char *str);
int strcmp(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
int string_format(char *buf, size_t size, const char *fmt, ...);

#endif
