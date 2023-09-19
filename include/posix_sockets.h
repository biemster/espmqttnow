#if !defined(__POSIX_SOCKET_TEMPLATE_H__)
#define __POSIX_SOCKET_TEMPLATE_H__

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <linux/filter.h>

/*
	A template for opening a non-blocking POSIX socket.
*/
int open_nb_socket(const char* addr, const char* port) {
	struct addrinfo hints = {0};

	hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Must be TCP */
	int sockfd = -1;
	int rv;
	struct addrinfo *p, *servinfo;

	/* get address information */
	rv = getaddrinfo(addr, port, &hints, &servinfo);
	if(rv != 0) {
		fprintf(stderr, "Failed to open socket (getaddrinfo): %s\n", gai_strerror(rv));
		return -1;
	}

	/* open the first possible socket */
	for(p = servinfo; p != NULL; p = p->ai_next) {
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1) continue;

		/* connect to server */
		rv = connect(sockfd, p->ai_addr, p->ai_addrlen);
		if(rv == -1) {
		  close(sockfd);
		  sockfd = -1;
		  continue;
		}
		break;
	}  

	/* free servinfo */
	freeaddrinfo(servinfo);

	/* make non-blocking */
#if !defined(WIN32)
	if (sockfd != -1) fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
#else
	if (sockfd != INVALID_SOCKET) {
		int iMode = 1;
		ioctlsocket(sockfd, FIONBIO, &iMode);
	}
#endif
#if defined(__VMS)
	/* 
		OpenVMS only partially implements fcntl. It works on file descriptors
		but silently fails on socket descriptors. So we need to fall back on
		to the older ioctl system to set non-blocking IO
	*/
	int on = 1;                 
	if (sockfd != -1) ioctl(sockfd, FIONBIO, &on);
#endif

	/* return the new socket fd */
	return sockfd;
}

int create_raw_socket(char *dev, struct sock_fprog *bpf) {
	struct sockaddr_ll sll;
	struct ifreq ifr;
	int fd, ifi, rb, attach_filter;

	bzero(&sll, sizeof(sll));
	bzero(&ifr, sizeof(ifr));

	fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	assert(fd != -1);

	strncpy((char *)ifr.ifr_name, dev, IFNAMSIZ);
	ifi = ioctl(fd, SIOCGIFINDEX, &ifr);
	assert(ifi != -1);

	sll.sll_protocol = htons(ETH_P_ALL);
	sll.sll_family = PF_PACKET;
	sll.sll_ifindex = ifr.ifr_ifindex;
	sll.sll_pkttype = PACKET_OTHERHOST;

	rb = bind(fd, (struct sockaddr *)&sll, sizeof(sll));
	assert(rb != -1);

	attach_filter = setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, bpf, sizeof(*bpf));
	assert(attach_filter != -1);

	return fd;
}

#endif
