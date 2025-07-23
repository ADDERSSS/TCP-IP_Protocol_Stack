#ifndef TOOLS_H
#define TOOLS_H 

#include <stdint.h>
#include "net_cfg.h"
#include "net_err.h"

static inline uint16_t swap_u16 (uint16_t x) {
    uint16_t r = ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
    return r;
}

static inline uint32_t swap_u32 (uint32_t x) {
    uint32_t r = ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
    return r;
}

#if NET_ENDIAN_LITTLE
#define x_htons(v) swap_u16(v)
#define x_ntohs(v) swap_u16(v)
#define x_htonl(v) swap_u32(v)
#define x_ntohl(v) swap_u32(v)
#else
#define x_htons(v)
#define x_ntohs(v)
#define x_htonl(v)
#define x_ntohl(v)
#endif

net_err_t tools_init (void);

#endif