#ifndef _SL_COMMONS_H
#define _SL_COMMONS_H

#include <linux/time.h>

unsigned long long to_usec(struct timespec64 *t);
unsigned long long diff_usec(struct timespec64 *t1, struct timespec64 *t2);
char toUpper(char c);

#endif
