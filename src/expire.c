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

#include <time.h>
#include <string.h>
#include <stdio.h>

static const int SEC = 1;
static const int MIN = 60;
static const int HOUR = 60*60;
static const int DAY = 24*60*60;
static const int WEEK = 7*24*60*60;
static const int YEAR = 365*24*60*60;

time_t txt2exp(const char *txt)
{
	time_t r, x;

	/* infinite time */
	if (!strcmp(txt, "always") || !strcmp(txt, "*"))
		return 0;

	/* parse */
	r = time(NULL);
	while(*txt) {
		x = 0;
		while('0' <= *txt && *txt <= '9')
			x = 10 * x + (time_t)(*txt++ - '0');
		switch(*txt) {
		case 'y': r += x * YEAR; txt++; break;
		case 'w': r += x * WEEK; txt++; break;
		case 'd': r += x * DAY; txt++; break;
		case 'h': r += x * HOUR; txt++; break;
		case 'm': r += x * MIN; txt++; break;
		case 's': txt++; /*@fallthrough@*/
		case 0: r += x * SEC; break;
		default: return -1;
		}
	}
	return r;
}

size_t exp2txt(time_t expire, char *buffer, size_t buflen)
{
	char b[100];
	size_t l;
	int n;

	if (!expire)
		strncpy(b, "always", sizeof b);
	else {
		expire -= time(NULL);
		n = 0;
#define ADD(C,U) \
  if (expire >= U) { \
    n += snprintf(&b[n], sizeof b - n, "%lld" #C, (long long)(expire / U)); \
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
