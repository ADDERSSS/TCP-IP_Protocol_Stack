#include "netif.h"
#include "mblock.h"
#include "dbg.h"
#include "exmsg.h"
#include "protocol.h"
#include "ether.h"

static netif_t netif_buffer[NETIF_DEV_CNT];
static mblock_t netif_mblock;
static nlist_t netif_list;
static netif_t * netif_default;

static const link_layer_t * link_layers[NETIF_TYPE_SIZE];

#if DBG_DISP_ENABLE(DBG_NETIF)

void display_netif_list (void) { 
    plat_printf("netif list:\n");

    nlist_node_t * node;
    nlist_for_each(node, &netif_list) { 
        netif_t * netif = nlist_entry(node, netif_t, node);
        plat_printf("%s", netif->name);
        switch (netif->state) {
            case NETIF_CLOSED:
                plat_printf(" %s ", "closed");
                break;
            case NETIF_OPENED:
                plat_printf(" %s ", "opened");
                break;
            case NETIF_ACTIVE:
                plat_printf(" %s ", "active");
                break;
            default:
                plat_printf(" %s ", "unknown state");
                break;
        }

        switch (netif->type) {
            case NETIF_TYPE_ETHER:
                plat_printf(" %s ", "ether");
                break;
            case NETIF_TYPE_LOOP:
                plat_printf(" %s ", "loop");
                break;
            default:
                plat_printf(" %s ", "unknown type");
                break;
        }

        plat_printf(" mtu=%d \n", netif->mtu);
        dbg_dump_hwaddr("hwaddr", netif->hwaddr.addr, netif->hwaddr.len);
        dbg_dump_ip("ipaddr", &netif->ipaddr);
        dbg_dump_ip("netmask", &netif->netmask);
        dbg_dump_ip("gateway", &netif->gateway);
        plat_printf(" \n");
    }
}

#else

#define display_netif_list () 

#endif

net_err_t netif_init (void) {
    dbg_info(DBG_NETIF, "netif init");

    nlist_init(&netif_list);
    mblock_init(&netif_mblock, netif_buffer, sizeof(netif_t), NETIF_DEV_CNT, NLOCKER_NONE);

    netif_default = (netif_t *)0;

    plat_memset((void *)link_layers, 0, sizeof(link_layers));

    dbg_info(DBG_NETIF, "init netif ok!");
    return NET_ERR_OK;
}

net_err_t netif_register_layer(int type, const link_layer_t * layer) {
    if ((type < 0) || (type >= NETIF_TYPE_SIZE)) {
        dbg_error(DBG_NETIF, "invalid type, type = %d", type);
        return NET_ERR_PARAM;
    }

    if (link_layers[type]) {
        dbg_error(DBG_NETIF, "link layer type %d already registered", type);
        return NET_ERR_EXIST;
    }

    link_layers[type] = layer;
    return NET_ERR_OK;
}

static const link_layer_t * netif_get_layer (int type) { 
    if ((type < 0) || (type >= NETIF_TYPE_SIZE)) {
        dbg_error(DBG_NETIF, "invalid type, type = %d", type);
        return (const link_layer_t *)0;
    }

    return link_layers[type];
}

netif_t * netif_open (const char * dev_name, const netif_ops_t * ops, void * ops_data) {
    netif_t * netif = (netif_t *)mblock_alloc(&netif_mblock, -1);
    if (!netif) {
        dbg_error(DBG_NETIF, "alloc netif failed!");
        return (netif_t *)0;
    }

    ipaddr_set_any(&netif->ipaddr);
    ipaddr_set_any(&netif->netmask);
    ipaddr_set_any(&netif->gateway);

    plat_strncpy(netif->name, dev_name, NETIF_NAME_SIZE);
    netif->name[NETIF_NAME_SIZE - 1] = '\0';

    plat_memset(&netif->hwaddr, 0, NETIF_HWADDR_SIZE);
    netif->type = NETIF_TYPE_NONE;
    netif->mtu = 0;
    nlist_node_init(&netif->node);

    net_err_t err = fixq_init(&netif->in_q, netif->in_q_buf, NET_INQ_SIZE, NLOCKER_THREAD);
    if (err < 0) {
        dbg_error(DBG_NETIF, "fixq_init failed, err = %d", err);
        mblock_free(&netif_mblock, netif);
        return (netif_t *)0;
    }


    err = fixq_init(&netif->out_q, netif->out_q_buf, NET_OUTQ_SIZE, NLOCKER_THREAD);
    if (err < 0) {
        dbg_error(DBG_NETIF, "fixq_init failed, err = %d", err);
        fixq_destroy(&netif->in_q);
        mblock_free(&netif_mblock, netif);
        return (netif_t *)0;
    }

    netif->ops = ops;
    netif->ops_data = ops_data;

    err = ops->open(netif, ops_data);
    if (err < 0) {
        dbg_error(DBG_NETIF, "ops open failed, err = %d", err);
        goto free_return;
    }

    netif->state = NETIF_OPENED;

    if (netif->type == NETIF_TYPE_NONE) {
        dbg_error(DBG_NETIF, "netif type is NONE");
        goto free_return;
    }

    netif->link_layer = netif_get_layer(netif->type);
    if (!netif->link_layer && (netif->type != NETIF_TYPE_LOOP)) {
        dbg_error(DBG_NETIF, "netif type is not registered");
        goto free_return;
    }

    nlist_insert_last(&netif_list, &netif->node);

    display_netif_list();

    return netif;

free_return:
    if (netif->state == NETIF_OPENED) {
        netif->ops->close(netif);
    }
    fixq_destroy(&netif->in_q);
    fixq_destroy(&netif->out_q);
    mblock_free(&netif_mblock, netif);
    return (netif_t *)0;
}

net_err_t netif_set_addr (netif_t * netif, ipaddr_t * ip, ipaddr_t * netmask, ipaddr_t * gateway){
    ipaddr_copy(&netif->ipaddr, ip ? ip : ipaddr_get_any());
    ipaddr_copy(&netif->netmask, netmask ? netmask : ipaddr_get_any());
    ipaddr_copy(&netif->gateway, gateway ? gateway : ipaddr_get_any());

    return NET_ERR_OK;
}


net_err_t netif_set_hwaddr (netif_t * netif, uint8_t * hwaddr, int len) {
    plat_memcpy(netif->hwaddr.addr, hwaddr, len);
    netif->hwaddr.len = len;
    return NET_ERR_OK;
}

net_err_t netif_set_active (netif_t * netif) {
    if (netif->state != NETIF_OPENED) {
        dbg_error(DBG_NETIF, "netif is not opened");
        return NET_ERR_STATE;
    }

    if (!netif_default && (netif->type != NETIF_TYPE_LOOP)) {
        netif_set_default(netif);
    }

    if (netif->link_layer) {
        net_err_t err = netif->link_layer->open(netif);
        if (err < 0) {
            dbg_error(DBG_NETIF, "link layer open failed");
            return err;
        }
    }

    netif->state = NETIF_ACTIVE;
    display_netif_list();
    return NET_ERR_OK;
}

net_err_t netif_set_deactive (netif_t * netif) {
    if (netif->state != NETIF_ACTIVE) {
        dbg_error(DBG_NETIF, "netif is not active");
        return NET_ERR_STATE;
    }

    if (netif->link_layer) {
        netif->link_layer->close(netif);
    }

    pktbuf_t * buf;
    while ((buf = fixq_recv(&netif->in_q, -1)) != (pktbuf_t *)0) {
        pktbuf_free(buf);
    }

    while ((buf = fixq_recv(&netif->out_q, -1)) != (pktbuf_t *)0) {
        pktbuf_free(buf);
    }
    if (netif_default == netif) {
        netif_default = (netif_t *)0;
    }

    netif->state = NETIF_OPENED;
    display_netif_list();
    return NET_ERR_OK;
}

net_err_t netif_close (netif_t * netif) {
    if (netif->state == NETIF_ACTIVE) {
        dbg_error(DBG_NETIF, "netif is active, can't close");
        return NET_ERR_STATE;
    }

    netif->ops->close(netif);
    netif->state = NETIF_CLOSED;

    nlist_remove(&netif_list, &netif->node);

    mblock_free(&netif_mblock, netif);
    display_netif_list();
    return NET_ERR_OK;
}

void netif_set_default (netif_t * netif) {
    netif_default = netif;
}

net_err_t netif_put_in (netif_t * netif, pktbuf_t * pktbuf, int ms) {
    net_err_t err = fixq_send(&netif->in_q, pktbuf, ms);
    if (err < 0) {
        dbg_warning(DBG_NETIF, "netif_put_in failed");
        return NET_ERR_FULL;
    }

    exmsg_netif_in(netif);
    return NET_ERR_OK;
}

pktbuf_t * netif_get_in (netif_t * netif, int ms) {
    pktbuf_t * pktbuf = fixq_recv(&netif->in_q, ms);
    if (pktbuf) {
        pktbuf_reset_acc(pktbuf);
        return pktbuf;
    }

    dbg_info(DBG_NETIF, "netif_get_in empty");
    return (pktbuf_t *)0;
}

net_err_t netif_put_out (netif_t * netif, pktbuf_t * pktbuf, int ms) {
    net_err_t err = fixq_send(&netif->out_q, pktbuf, ms);
    if (err < 0) {
        dbg_warning(DBG_NETIF, "netif_put_out failed");
        return NET_ERR_FULL;
    }

    return NET_ERR_OK;
}

pktbuf_t * netif_get_out (netif_t * netif, int ms) {
    pktbuf_t * pktbuf = fixq_recv(&netif->out_q, ms);
    if (pktbuf) {
        pktbuf_reset_acc(pktbuf);
        return pktbuf;
    }

    dbg_info(DBG_NETIF, "netif_get_out empty");
    return (pktbuf_t *)0;
}

net_err_t netif_out(netif_t * netif, ipaddr_t * ipaddr, pktbuf_t * buf) {
    if (netif->link_layer) {
        net_err_t err = ether_raw_out(netif, NET_PROTOCOL_ARP, ether_broadcast_addr(), buf);
        if (err < 0) {
            dbg_warning(DBG_NETIF, "netif_out ether_raw_out error");
            return err;
        }

        return NET_ERR_OK;
    } else {
        net_err_t err = netif_put_out(netif, buf, -1);
        if (err < 0) {
            dbg_info(DBG_NETIF, "send failed");
            return err;
        }
    }

    return netif->ops->xmit(netif);
}