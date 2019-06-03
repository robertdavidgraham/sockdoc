#include "util-ctype.h"



static const unsigned char ctype_data[] = {
0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x60, 0x60, 0x60, 0x60, 0x60, 0x40, 0x40, 
0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 
0xa0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
0x90, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 
0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x90, 0x90, 0x90, 0x90, 0x90, 
0x90, 0x86, 0x86, 0x86, 0x86, 0x86, 0x86, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 
0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x90, 0x90, 0x90, 0x90,
};

int ISDIGIT(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x01) != 0;
}

int ISXDIGIT(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x02) != 0;
}

int ISLOWER(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x04) != 0;
}

int ISUPPER(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x08) != 0;
}

int ISALPHA(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x0C) != 0;
}

int ISALNUM(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x0D) != 0;
}

int ISPUNCT(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x10) != 0;
}

int ISSPACE(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x20) != 0;
}

int ISCNTRL(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x040) != 0;
}

int ISPRINT(int x)
{
    if (x < 0 || 127 < x)
        return 0;
    return (ctype_data[x] & 0x80) != 0;
}

int TOUPPER(int x)
{
    if (ISLOWER(x))
        return x & ~32;
    else
        return x;
}

int TOLOWER(int x)
{
    if (ISUPPER(x))
        return x | 32;
    else
        return x;
}

#ifdef SELFTEST
#include <ctype.h>
#include <stdio.h>
int util_ctype_selftest(void)
{
    int c;
    for (c=0; c<127; c++) {
        if (isalnum(c) && !ISALNUM(c))
            return 1;
        if (isxdigit(c) && !ISXDIGIT(c))
            return 1;
        if (ispunct(c) && !ISPUNCT(c))
            return 1;
        if (isspace(c) && !ISSPACE(c))
            return 1;
        if (iscntrl(c) && !ISCNTRL(c))
            return 1;
        if (isprint(c) && !ISPRINT(c))
            return 1;
        if (tolower(c) != TOLOWER(c))
            return 1;
        if (toupper(c) != TOUPPER(c))
            return 1;
    }
    return 0;
}
int main(void)
{
    if (util_ctype_selftest())
        printf("fail\n");
    else
        printf("success\n");
    
}
#endif

