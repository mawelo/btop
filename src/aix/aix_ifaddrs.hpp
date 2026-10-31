/* Copyright 2026 Martin Lonkwitz (btop-aix)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

// getifaddrs(3)/freeifaddrs(3) for AIX.
//
// AIX 7.1 through 7.3 ship neither <ifaddrs.h> nor getifaddrs() in libc
// (checked 2026-10-12 on 7100-05-03 and 7200-05-05: no header, no export).
// This header provides the same interface on top of the SIOCGSIZIFCONF /
// SIOCGIFCONF / SIOCGIFFLAGS ioctls, so that the shared IfAddrsPtr wrapper in
// btop_shared.hpp works unchanged. SIOCGIFCONF on AIX returns one entry per
// address of every family (AF_LINK, AF_INET, AF_INET6) with BSD style variable
// length sockaddrs (sa_len).
//
// Only the members btop uses are filled: ifa_next, ifa_name, ifa_flags and
// ifa_addr. ifa_netmask, ifa_dstaddr and ifa_data are always nullptr.

#pragma once

#if defined(_AIX)

#include <sys/socket.h>
#include <net/if.h>

struct ifaddrs {
	struct ifaddrs* ifa_next;
	char* ifa_name;
	unsigned int ifa_flags;
	struct sockaddr* ifa_addr;
	struct sockaddr* ifa_netmask;
	struct sockaddr* ifa_dstaddr;
	void* ifa_data;
};

extern "C" {
	//* Returns 0 on success and sets *ifap to a malloc()ed list, -1 with errno set otherwise.
	int getifaddrs(struct ifaddrs** ifap);
	//* Releases a list returned by getifaddrs(); accepts nullptr.
	void freeifaddrs(struct ifaddrs* ifa);
}

#endif // _AIX
