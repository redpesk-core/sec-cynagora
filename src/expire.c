/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/******************************************************************************/
/******************************************************************************/
/* CONVERTION OF EXPIRATIONS TO AND FROM TEXT                                 */
/******************************************************************************/
/******************************************************************************/

#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "expire.h"

static const int SEC = 1;
static const int MIN = 60;
static const int HOUR = 60*60;
static const int DAY = 24*60*60;
static const int WEEK = 7*24*60*60;
static const int YEAR = 365*24*60*60 + 24*60*60/4; /* average includes leap */
static const time_t TMIN = (time_t)1 << ((CHAR_BIT * sizeof(time_t)) - 1);
static const time_t TMAX = ~TMIN;

/** add positives x and y with saturation */
static time_t pt_add(time_t x, time_t y)
{
	time_t r = x + y;
	return r < 0 ? TMAX : r;
}

/** multiply positive x by m with saturation */
static time_t pt_mul(time_t x, int m)
{
	time_t r;

	if (m <= 1)
		r = 0;
	else {
		r = pt_mul(x, m >> 1) << 1;
		if (r < 0)
			r = TMAX;
	}
	return (m & 1) ? pt_add(r, x) : r;
}

/** multiply positive x by m and then add y with saturation */
static time_t pt_muladd(time_t x, int m, time_t y)
{
	return pt_add(pt_mul(x, m), y);
}

/** multiply positive x by 10 and then add d with saturation */
static time_t pt_tm10a(time_t x, int d)
{
	return pt_muladd(x, 10, (time_t)d);
}

/** translate the string 'txt' to its time representation */
static bool parse_time_spec(const char *txt, time_t *time_out)
{
	time_t r, x;

	/* parse */
	r = 0;
	while(*txt) {
		x = 0;
		while('0' <= *txt && *txt <= '9')
			x = pt_tm10a(x, *txt++ - '0');
		switch(*txt) {
		case 'y': r = pt_muladd(x, YEAR, r); txt++; break;
		case 'w': r = pt_muladd(x, WEEK, r); txt++; break;
		case 'd': r = pt_muladd(x, DAY, r); txt++; break;
		case 'h': r = pt_muladd(x, HOUR, r); txt++; break;
		case 'm': r = pt_muladd(x, MIN, r); txt++; break;
		case 's': txt++; /*@fallthrough@*/
		case 0: r = pt_muladd(x, SEC, r); break;
		default: return false;
		}
	}
	*time_out = r;
	return true;
}


/* see expire.h */
bool txt2exp(const char *txt, time_t *time_out, bool absolute)
{
	bool nocache;
	time_t r;

	/* no cache */
	nocache = txt[0] == '-';
	txt += nocache;

	/* infinite time */
	if (!txt[0] || !strcmp(txt, "always") || !strcmp(txt, "forever") || !strcmp(txt, "*")) {
		r = 0;
	} else {
		/* parse */
		if (!parse_time_spec(txt, &r))
			return false;
		if (absolute) {
			/* absolute time */
			r = pt_add(r, time(NULL));
		}
	}

	*time_out = nocache ? -(r + 1) : r;
	return true;
}

/* see expire.h */
size_t exp2txt(time_t expire, bool absolute, char *buffer, size_t buflen)
{
	char b[100];
	size_t l, n;

	n = 0;
	if (expire < 0) {
		b[n++] = '-';
		b[n] = 0;
		expire = -(expire + 1);
	}
	if (!expire) {
		if (!n)
			strncpy(b, "forever", sizeof b);
	} else {
		if (absolute)
			expire -= time(NULL);
#define ADD(C,U) \
  if (expire >= U) { \
    n += (size_t)snprintf(&b[n], sizeof b - (size_t)n, "%lld" #C, (long long)(expire / U)); \
    expire %= U; \
  }
		ADD(y,YEAR)
		ADD(w,WEEK)
		ADD(d,DAY)
		ADD(h,HOUR)
		ADD(m,MIN)
		ADD(s,SEC)
#undef ADD
	}
	l = strlen(b);
	strncpy(buffer, b, buflen);
	return l;
}
