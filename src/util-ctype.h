#ifndef UTIL_CTYPE_H
#define UTIL_CTYPE_H
/*
    functions for parsing text within network protocols

    Classic ctype function like 'isdigit()' or 'tolower()'
    are undefined for network input. They are defined for
    parsing text files in the internal character set, which
    on IBM mainframes isn't even ASCII. Their exact behavior
    varies from platform to platform when characters are 
    outside the ASCII range (e.g. greater than 127).


    This file defines alternate versions appropriate for
    parsing network input, removing all undefined behavior,
    so that they act the same on all platforms regardless
    of input.

    The differences are:

    - the input character is 7-bit ASCII. In other words,
      ISALPHA(0x41) is always true.
    - for values less than 0 and greater than 127,
      the ISxxxx() functions return 0.
    - truth values are defined as 1, false values are 0.
    - the TOUPPER()/TOLOWER() functions return the same
      value as input unless the input is a lower/upper
      character respectively.
*/

int ISALNUM(int c);
int ISCNTRL(int c);
int ISDIGIT(int c);
int ISGRAPH(int c);
int ISLOWER(int c);
int ISPRINT(int c);
int ISPUNCT(int c);
int ISSPACE(int c);
int ISUPPER(int c);
int ISXDIGIT(int c);
int TOLOWER(int c);
int TOUPPER(int c);

#endif
