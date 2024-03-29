/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/* ifaddrs.h -- declarations for getting network interface addresses
   Copyright (C) 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef GETIFADDRS_H
#define GETIFADDRS_H

/**
 * 02-12-26, tim@tjansen.de: added kde_ prefix, fallback-code,
 *                           removed glibs dependencies
 */

#include "config.h"

#include <string>

#ifdef _WIN32
#include <winsock2.h>
#undef interface
#else
#include <sys/types.h>

#include <sys/socket.h>
#include <net/if.h>
#endif

#ifndef IFF_POINTOPOINT
#   define IFF_POINTOPOINT 0x10
#endif

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>

#define kde_getifaddrs(a) getifaddrs(a)
#define kde_freeifaddrs(a) freeifaddrs(a)
#define kde_ifaddrs ifaddrs

#else

/* The `getifaddrs' function generates a linked list of these structures.
   Each element of the list describes one network interface.  */
struct kde_ifaddrs {
    struct kde_ifaddrs *ifa_next; /* Pointer to the next structure.  */

    char *ifa_name;       /* Name of this network interface.  */
    unsigned int ifa_flags;   /* Flags as from SIOCGIFFLAGS ioctl.  */

    struct sockaddr *ifa_addr;    /* Network address of this interface.  */
    struct sockaddr *ifa_netmask; /* Netmask of this interface.  */

    union {
        /* At most one of the following two is valid.  If the IFF_BROADCAST
           bit is set in `ifa_flags', then `ifa_broadaddr' is valid.  If the
           IFF_POINTOPOINT bit is set, then `ifa_dstaddr' is valid.
           It is never the case that both these bits are set at once.  */
        struct sockaddr *ifu_broadaddr; /* Broadcast address of this interface. */
        struct sockaddr *ifu_dstaddr; /* Point-to-point destination address.  */
    } ifa_ifu;

    /* These very same macros are defined by <net/if.h> for `struct ifaddr'.
       So if they are defined already, the existing definitions will be fine.  */
# ifndef ifa_broadaddr
#  define ifa_broadaddr ifa_ifu.ifu_broadaddr
# endif
# ifndef ifa_dstaddr
#  define ifa_dstaddr ifa_ifu.ifu_dstaddr
# endif

    void *ifa_data;       /* Address-specific data (may be unused).  */
};


/* Create a linked list of `struct kde_ifaddrs' structures, one for each
   network interface on the host machine.  If successful, store the
   list in *IFAP and return 0.  On errors, return -1 and set `errno'.

   The storage returned in *IFAP is allocated dynamically and can
   only be properly freed by passing it to `freeifaddrs'.  */
extern int kde_getifaddrs(struct kde_ifaddrs **__ifap);

/* Reclaim the storage allocated by a previous `getifaddrs' call.  */
extern void kde_freeifaddrs(struct kde_ifaddrs *__ifa);

#endif

/**
 * Constructs an IPv4 socket address for a given port and network interface.
 *
 * The address is suitable for use by a subsequent call to bind().
 * If the interface argument is an empty string, the socket will listen on all interfaces.
 */
bool build_address_for_interface(struct sockaddr_in &myaddr, const std::string &interface, int port);

#endif
