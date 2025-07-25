#ifndef NETIF_PCAP_H
#define NETIF_PCAP_H

#include "net_err.h"
#include "netif.h"

typedef struct _pacp_data_t {
    const char * ip;
    const uint8_t * hwaddr;
} pcap_data_t;

extern const netif_ops_t netdev_ops;


#endif