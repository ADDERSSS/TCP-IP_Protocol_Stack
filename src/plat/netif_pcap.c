#include "netif_pcap.h"
#include "sys_plat.h"
#include "exmsg.h"
#include "pcap.h"
#include "dbg.h"
#include "ether.h"

void recv_thread (void * arg) {
    plat_printf("recv thread is running....\n");

    netif_t * netif = (netif_t *)arg;
    pcap_t * pcap = (pcap_t *)netif->ops_data;
    while (1) {
        struct pcap_pkthdr * pkt_hdr;
        const uint8_t * pkt_data;
        if(pcap_next_ex(pcap, &pkt_hdr, &pkt_data) != 1) {
            continue;
        }

        pktbuf_t * buf = pktbuf_alloc(pkt_hdr->len);
        if (buf == (pktbuf_t *)0) {
            dbg_warning(DBG_NETIF, "pktbuf alloc failed!\n");
            continue;
        }

        pktbuf_write(buf, (uint8_t *)pkt_data, pkt_hdr->len);

        if(netif_put_in(netif, buf, 0) < 0) {
            dbg_warning(DBG_NETIF, "netif %s put in failed!\n", netif->name);
            pktbuf_free(buf);
            continue;
        }
    }
}

void xmit_thread (void * arg) {
    plat_printf("xmit thread is running....\n");
    netif_t * netif = (netif_t *)arg;
    pcap_t * pcap = (pcap_t *)netif->ops_data;
    static uint8_t rw_buffer[1500 + 6 + 6 + 2];
    while (1) {
        pktbuf_t * buf = netif_get_out(netif, 0);
        if (buf == (pktbuf_t *)0) {
            continue;
        }

        int total_size = buf->total_size;
        plat_memset(rw_buffer, 0, sizeof(rw_buffer));
        pktbuf_read(buf, rw_buffer, total_size);
        pktbuf_free(buf);

        if(pcap_inject(pcap, rw_buffer, total_size) == -1) {
            plat_printf("pacp send failed: %s | size: %d\n", pcap_geterr(pcap), total_size);
            continue;
        }
    }
}


static net_err_t netif_pacp_open (netif_t * netif, void * data) { 
    pcap_data_t * dev_data = (pcap_data_t *)data;

    pcap_t * pcap = pcap_device_open(dev_data->ip, dev_data->hwaddr);
    if (pcap == (pcap_t *)0) {
        dbg_error(DBG_NETIF, "pcap open failed! name: %s\n", netif->name);
        return NET_ERR_IO;
    }

    netif->type = NETIF_TYPE_ETHER;
    netif->mtu = ETHER_MTU;
    netif->ops_data = pcap;
    netif_set_hwaddr(netif, (uint8_t *)dev_data->hwaddr, 6);

    sys_thread_create(recv_thread, netif);
    sys_thread_create(xmit_thread, netif);
    return NET_ERR_OK;
}

static void netif_pacp_close (netif_t * netif) { 
    dbg_info(DBG_NETIF, "loop close");
    pcap_t * pcap = (pcap_t *)netif->ops_data;
    pcap_close(pcap);
}

static net_err_t netif_pacp_xmit (netif_t * netif) { 
    dbg_info(DBG_NETIF, "loop xmit");
    return NET_ERR_OK;
}

const netif_ops_t netdev_ops = {
    .close = netif_pacp_close,
    .open = netif_pacp_open,
    .xmit = netif_pacp_xmit,
};