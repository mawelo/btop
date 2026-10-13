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

// Implementation of getifaddrs()/freeifaddrs() for AIX, see aix_ifaddrs.hpp.
//
// Algorithm (O(n) in the number of addresses, one ioctl per interface for the
// flags): SIOCGSIZIFCONF tells the buffer size, SIOCGIFCONF fills a packed
// array of {ifr_name[IFNAMSIZ], sockaddr(sa_len)} records. Each record becomes
// one list node allocated as a single malloc() block holding the node, the
// name and a copy of the sockaddr, so freeifaddrs() is a plain free() per node.

#include "aix_ifaddrs.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {
	struct FdGuard {
		int fd;
		explicit FdGuard(int f) : fd(f) {}
		~FdGuard() { if (fd >= 0) ::close(fd); }
		FdGuard(const FdGuard&) = delete;
		FdGuard& operator=(const FdGuard&) = delete;
	};

	unsigned int iface_flags(int sock, const char* name) {
		struct ifreq req;
		std::memset(&req, 0, sizeof req);
		std::strncpy(req.ifr_name, name, IFNAMSIZ - 1);
		if (::ioctl(sock, SIOCGIFFLAGS, &req) != 0) return 0;
		return static_cast<unsigned int>(req.ifr_flags);
	}
}

extern "C" int getifaddrs(struct ifaddrs** ifap) {
	if (ifap == nullptr) { errno = EINVAL; return -1; }
	*ifap = nullptr;

	FdGuard sock(::socket(AF_INET, SOCK_DGRAM, 0));
	if (sock.fd < 0) return -1;

	int size = 0;
	if (::ioctl(sock.fd, SIOCGSIZIFCONF, &size) != 0 or size <= 0) return -1;

	std::vector<char> buf(static_cast<size_t>(size));
	struct ifconf ifc;
	std::memset(&ifc, 0, sizeof ifc);
	ifc.ifc_len = size;
	ifc.ifc_buf = buf.data();
	if (::ioctl(sock.fd, SIOCGIFCONF, &ifc) != 0) return -1;

	struct ifaddrs* head = nullptr;
	struct ifaddrs* tail = nullptr;

	const char* p = buf.data();
	const char* end = buf.data() + ifc.ifc_len;
	while (p + IFNAMSIZ + sizeof(struct sockaddr) <= end) {
		const struct ifreq* req = reinterpret_cast<const struct ifreq*>(p);
		const size_t sa_len = std::max<size_t>(req->ifr_addr.sa_len, sizeof(struct sockaddr));
		const size_t record = IFNAMSIZ + sa_len;
		if (p + record > end) break;

		char name[IFNAMSIZ + 1];
		std::memcpy(name, req->ifr_name, IFNAMSIZ);
		name[IFNAMSIZ] = '\0';

		//? ifa_addr must be suitably aligned for sockaddr_in/sockaddr_in6 (callers cast and read 4/16 byte fields):
		//? round the offset up to the strictest alignment instead of placing it right behind the name
		const size_t addr_off = (sizeof(struct ifaddrs) + sizeof name + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
		const size_t need = addr_off + sa_len;
		char* block = static_cast<char*>(std::malloc(need));
		if (block == nullptr) {
			freeifaddrs(head);
			errno = ENOMEM;
			return -1;
		}
		std::memset(block, 0, need);
		struct ifaddrs* node = reinterpret_cast<struct ifaddrs*>(block);
		node->ifa_name = block + sizeof(struct ifaddrs);
		std::memcpy(node->ifa_name, name, sizeof name);
		node->ifa_addr = reinterpret_cast<struct sockaddr*>(block + addr_off);
		std::memcpy(node->ifa_addr, &req->ifr_addr, sa_len);
		node->ifa_flags = iface_flags(sock.fd, name);

		if (tail == nullptr) head = node; else tail->ifa_next = node;
		tail = node;
		p += record;
	}

	*ifap = head;
	return 0;
}

extern "C" void freeifaddrs(struct ifaddrs* ifa) {
	while (ifa != nullptr) {
		struct ifaddrs* next = ifa->ifa_next;
		std::free(ifa);
		ifa = next;
	}
}
