/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


/* "More computing sins are committed in the name of
 * efficiency (without necessarily achieving it) than
 * for any other single reason - including blind
 * stupidity."  -- W.A. Wulf
 */



#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "tempfile.h"
#include "exitcode.h"


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static char temp_dir_path[MAX_PATH + 1] = {0};

static void prep_temp_dir_path(void) {
    DWORD len = GetTempPathA(sizeof(temp_dir_path), temp_dir_path);
    temp_dir_path[len - 1] = '\0';
}

static char * mkdtemp(char * template) {
    unsigned int len = strlen(template);
    for(unsigned int i = len - 7; i < len; ++i) {
        if(template[i] != 'X') {
            errno = EINVAL;
            return NULL;
        }
    }

    char * buf = malloc(len + 1);
    if(!buf) {
        errno = ENOMEM;
        return NULL;
    }

    // From below
    unsigned long random_bits = (unsigned long) getpid() << 16;

    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        random_bits ^= tv.tv_usec << 16;
        random_bits ^= tv.tv_sec;
    }

    strncpy(buf, template, len + 1);
    snprintf(buf + len - 7, 6, "%06x", (unsigned int) (random_bits & 0xFFFFFF));

    if(mkdir(buf) < 0) {
        free(buf);
        return NULL;
    }

    strncpy(template, buf, len + 1);
    free(buf);
    return template;
}
#else
#ifndef _PATH_TMP
#define _PATH_TMP "/tmp"
#endif
static char temp_dir_path[] = _PATH_TMP;

static void prep_temp_dir_path(void) {}
#endif



/**
 * Create a file inside the temporary directory and register it for
 * later cleanup, and return its name.
 *
 * The file will be reopened later, possibly in a child.  But we know
 * that it exists with appropriately tight permissions.
 **/
int dcc_make_tmpnam(const char *prefix, const char *suffix, char **name_ret, int relative)
{
    unsigned long random_bits;
    unsigned long tries = 0;
    size_t tmpname_length;
    char *tmpname;

    if(temp_dir_path[0] == '\0')
        prep_temp_dir_path();

    tmpname_length = strlen(temp_dir_path) + 1 + strlen(prefix) + 1 + 8 + strlen(suffix) + 1;
    tmpname = malloc(tmpname_length);

    if (!tmpname) {
        return EXIT_OUT_OF_MEMORY;
    }

    random_bits = (unsigned long) getpid() << 16;

    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        random_bits ^= tv.tv_usec << 16;
        random_bits ^= tv.tv_sec;
    }

#if 0
    random_bits = 0;            /* FOR TESTING */
#endif

    do {
        if (snprintf(tmpname, tmpname_length, "%s/%s_%08lx%s",
                     (relative ? temp_dir_path + 1 : temp_dir_path),
                     prefix,
                     random_bits & 0xffffffffUL,
                     suffix) == -1) {
            free(tmpname);
            return EXIT_OUT_OF_MEMORY;
        }

        /* Note that if the name already exists as a symlink, this
         * open call will fail.
         *
         * The permissions are tight because nobody but this process
         * and our children should do anything with it. */
        int fd = open(tmpname, O_WRONLY | O_CREAT | O_EXCL, 0600);

        if (fd == -1) {
            /* Don't try getting a file too often.  Safety net against
               endless loops. Probably just paranoia.  */
            if (++tries > 1000000) {
                free(tmpname);
                return EXIT_IO_ERROR;
            }

            /* Some errors won't change by changing the filename,
               e.g. ENOENT means that the directory where we try to create
               the file was removed from under us.  Don't endlessly loop
               in that case.  */
            switch (errno) {
            case EACCES:
            case EEXIST:
            case EISDIR:
            case ELOOP:
                /* try again */
                random_bits += 7777; /* fairly prime */
                continue;
            }

            free(tmpname);
            return EXIT_IO_ERROR;
        }

        if (close(fd) == -1) {  /* huh? */
            free(tmpname);
            return EXIT_IO_ERROR;
        }

        break;
    } while (1);

    *name_ret = tmpname;

    return 0;
}

int dcc_make_tmpdir(char **name_ret) {
    if(temp_dir_path[0] == '\0')
        prep_temp_dir_path();

    unsigned long tries = 0;
    char template[] = "icecc-XXXXXX";
    size_t tmpname_length = strlen(temp_dir_path) + 1 + strlen(template) + 1;
    char *tmpname = malloc(tmpname_length);

    if (!tmpname) {
        return EXIT_OUT_OF_MEMORY;
    }

    if (snprintf(tmpname, tmpname_length, "%s/%s", temp_dir_path, template) == -1) {
        free(tmpname);
        return EXIT_OUT_OF_MEMORY;
    }

    do {
        if (!mkdtemp(tmpname)) {
            if (++tries > 1000000) {
                free(tmpname);
                return EXIT_IO_ERROR;
            }

            switch (errno) {
            case EACCES:
            case EEXIST:
            case EISDIR:
            case ELOOP:
                continue;
            }

            free(tmpname);
            return EXIT_IO_ERROR;
        }

        break;
    } while (1);

    *name_ret = tmpname;

    return 0;
}
