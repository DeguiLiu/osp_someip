/**
 * @file lwipopts.h
 * @brief lwIP options for the compile-only check of the lwIP socket backend.
 *
 * This configuration exists so `osp/socket.hpp` can be type-checked against
 * real lwIP headers on a host. It is not a product configuration.
 */

#ifndef OSP_LWIP_PORT_LWIPOPTS_H_
#define OSP_LWIP_PORT_LWIPOPTS_H_

#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define MEM_LIBC_MALLOC 1
#define LWIP_COMPAT_SOCKETS 0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
#define LWIP_SO_RCVTIMEO 1
#define LWIP_SO_SNDTIMEO 1
/* The host already provides struct timeval through <sys/select.h>. */
#define LWIP_TIMEVAL_PRIVATE 0
#define LWIP_HAVE_LOOPIF 1

#endif  // OSP_LWIP_PORT_LWIPOPTS_H_
