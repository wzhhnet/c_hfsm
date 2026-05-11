/*
 * Loger for HFSM
 *
 * Author wanch
 * Date 2021/12/27
 * Email wzhhnet@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <log.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdio.h>

void hfsm_trace(const char *style, const char *format, ...) {
    size_t n;
    char buf[4096];
    char timebuf[64];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    n = strftime(timebuf, sizeof(timebuf)-1, "%F|%T", tm);
    n = snprintf(buf, sizeof(buf)-1, "%s[%s.%06ld]", style, timebuf, tv.tv_usec);

    va_list ap;
    va_start(ap, format);
    n += vsnprintf(buf+n, sizeof(buf)-1-n, format, ap);
    if (n > 0 && n < (int)(sizeof(buf)-1)) {
        fprintf(stderr, "%s\n" RST, buf);
    }
    va_end (ap);
}

