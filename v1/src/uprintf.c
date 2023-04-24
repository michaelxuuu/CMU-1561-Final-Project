#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

void uputc(char c) {
    write(STDOUT_FILENO, &c, 1);
}

void printint(long x, int l, int u, int base) {
    if (!x) {
        uputc('0');
        return;
    }

    char buf[32] = {0};

    int neg = u & ((l && x >> 63) || (!l && x >> 31));
    unsigned long xx = neg ? -x : x;

    int i = 0;
    for (; xx; i++, xx /= base) {
        long rem = xx % base;
        char c = '0' + rem;
        if (rem > 9)
            c = 'a' + rem - 10;
        buf[i] = c;
    }

    if (neg) uputc('-');
    for (; i >= 0; i--)
        uputc(buf[i]);
}

void printstr(char *s) {
    for (int i = 0; s[i]; i++)
        uputc(s[i]);
}

void printptr(unsigned long x) {
    uputc('0');
    uputc('x');

    char buf[16] = {'0'};
    
    int i = 0;
    for (; x; i++, x /= 16) {
        int rem = x % 16;
        char c = '0' + rem;
        if (rem > 9)
            c = 'a' + rem - 10;
        buf[i] = c;
    }

    for (; i >= 0; i--)
        uputc(buf[i]);
}

// %d, %x, %p, %s
void uprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '%') {
            char c = fmt[++i];
            char cc = fmt[i+1];
            if (c == 's') printstr(va_arg(ap, char*));
            else if (c == 'd') printint(va_arg(ap, int), 0, 0, 10);
            else if (c == 'u') printint(va_arg(ap, int), 0, 1, 10);
            else if (c == 'x') printint(va_arg(ap, int), 0, 1, 16);
            else if (c == 'l' && cc == 'd') printint(va_arg(ap, long), 1, 0, 10), i++;
            else if (c == 'l' && cc == 'u') printint(va_arg(ap, long), 1, 1, 10), i++;
            else if (c == 'l' && cc == 'x') printint(va_arg(ap, long), 1, 1, 16), i++;
            else if (c == 'p') printptr(va_arg(ap, unsigned long));
            else if (c == 0) break;
            else uputc(fmt[i]);
        }
        else uputc(fmt[i]);
    }
    va_end(ap);
}
