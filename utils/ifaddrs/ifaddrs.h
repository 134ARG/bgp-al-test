#ifndef BROBABLBGP_UTILS_IFADDRS_H
#define BROBABLBGP_UTILS_IFADDRS_H

#include <ifaddrs.h>
#include <netdb.h>

int get_addr_str(struct sockaddr* ifa_addr, char* str);

struct ifaddrs* get_valid_ifs(struct ifaddrs* ifap,
                              int             accept_ipv6,
                              int             accept_lo);

unsigned int len_ifs(struct ifaddrs* ifs);

void print_ifaddrs(struct ifaddrs* ifap);

struct ifaddrs* find_matched_if(struct ifaddrs*     all_ifs,
                                struct sockaddr_in* addr);

#endif  // BROBABLBGP_UTILS_IFADDRS_H
