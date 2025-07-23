#include "ether.h"
#include "netif.h"
#include "dbg.h"
#include "tools.h"
#include "protocol.h"

#if DBG_DISP_ENABLE(DBG_ETHER)
static void display_ether_pkt (char * title, ether_pkt_t * pkt, int total_size) {
    ether_hdr_t * hdr = &pkt->hdr;
    plat_printf("----------------%s----------------\n", title);
    plat_printf("\t len: %d bytes\n", total_size);
    dbg_dump_hwaddr("\t dest: ", hdr->dest, ETHER_HWA_SIZE);
    dbg_dump_hwaddr("\t src: ", hdr->src, ETHER_HWA_SIZE);
    plat_printf("\t protocol: %04x\n", x_ntohs(hdr->protocol));

    switch (x_ntohs(hdr->protocol)) {
        case NET_PROTOCOL_ARP:
            plat_printf("\t ARP packet\n");
            break;
        case NET_PROTOCOL_IPv4:
            plat_printf("\t IPv4 packet\n");
            break;
        default:
            plat_printf("\t Unknown protocol\n");
            break;
    }

    plat_printf("-------------------------------------------\n");
}
#else
#define display_ether_pkt(title, pkt, total_size)
#endif

static net_err_t ether_open (struct _netif_t * netif) {
    return NET_ERR_OK;
}
static void ether_close (struct _netif_t * netif) {

}

static net_err_t is_pkt_ok (ether_pkt_t * pkt, int size) {
    if (size > (ETHER_MTU + sizeof(ether_pkt_t) - 1)) {
        dbg_error(DBG_ETHER, "packet size too big! %d\n", size);
        return NET_ERR_SIZE;
    }

    if (size < sizeof(ether_hdr_t)) {
        dbg_error(DBG_ETHER, "packet size too small! %d\n", size);
        return NET_ERR_SIZE;
    }

    return NET_ERR_OK;
}

static net_err_t ether_in (struct _netif_t * netif, pktbuf_t * buf) {
    dbg_info(DBG_ETHER, "ethernet in\n");

    pktbuf_set_cont(buf, sizeof(ether_hdr_t));
    ether_pkt_t * pkt = (ether_pkt_t *)pktbuf_data(buf);
    
    net_err_t err;
    if ((err = is_pkt_ok(pkt, buf->total_size)) < 0) {
        dbg_warning(DBG_ETHER, "pkt error\n");
        return err;
    }

    display_ether_pkt("ethernet in", pkt, buf->total_size);
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static net_err_t ether_out (struct _netif_t * netif, ipaddr_t * dest, pktbuf_t * buf) {
    return NET_ERR_OK;
}

net_err_t ether_init (void) {
    static const link_layer_t ether_layer = {
        .type = NETIF_TYPE_ETHER,
        .close = ether_close,
        .in = ether_in,
        .open = ether_open,
        .out = ether_out,
    };
    
    dbg_info(DBG_ETHER, "init ethernet");

    net_err_t err = netif_register_layer(NETIF_TYPE_ETHER, &ether_layer);
    if (err < 0) {
        dbg_error(DBG_ETHER, "register ethernet layer failed!");
        return err;
    }

    dbg_info(DBG_ETHER, "init ethernet done!");

    return NET_ERR_OK;
}

const uint8_t * ether_broadcast_addr (void) {
    static const uint8_t boardcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    return boardcast; 
}

net_err_t ether_raw_out (netif_t * netif, uint16_t protocol, uint8_t * dest, pktbuf_t * buf) {
    int size = pktbuf_total(buf);
    if (size < ETHER_DATA_MIN) {
        dbg_info(DBG_ETHER, "packet size is too small!");

        net_err_t err = pktbuf_resize(buf, ETHER_DATA_MIN);
        if (err < 0) {
            dbg_error(DBG_ETHER, "resize packet failed!");
            return err;
        }

        pktbuf_reset_acc(buf);
        pktbuf_seek(buf, size);
        pktbuf_fill(buf, 0, ETHER_DATA_MIN - size);

        size = ETHER_DATA_MIN;
    }

    net_err_t err = pktbuf_add_header(buf, sizeof(ether_hdr_t), 1);
    if (err < 0) {
        dbg_error(DBG_ETHER, "add header failed!");
        return err;
    }

    ether_pkt_t * pkt = (ether_pkt_t * )pktbuf_data(buf);
    plat_memcpy(pkt->hdr.dest, dest, ETHER_HWA_SIZE);
    plat_memcpy(pkt->hdr.src, netif->hwaddr.addr, ETHER_HWA_SIZE);
    pkt->hdr.protocol = x_htons(protocol);

    display_ether_pkt("ether out", pkt, size);

    if (plat_memcmp(dest, netif->hwaddr.addr, ETHER_HWA_SIZE) == 0) {
        return netif_put_in(netif, buf, -1);
    } else {
        err = netif_put_out(netif, buf, -1);
        if (err < 0) {
            dbg_error(DBG_ETHER, "put out failed!");
            return err;
        }

        return netif->ops->xmit(netif);
    }
}


