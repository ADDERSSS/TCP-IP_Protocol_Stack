#include "loop.h"
#include "dbg.h"
#include "exmsg.h"

static net_err_t loop_open (netif_t * netif, void * data) { 
    netif->type = NETIF_TYPE_LOOP;
    return NET_ERR_OK;
}

static void loop_close (netif_t * netif) { 
    dbg_info(DBG_NETIF, "loop close");
}

static net_err_t loop_xmit (netif_t * netif) { 
    dbg_info(DBG_NETIF, "loop xmit");
    pktbuf_t * pktbuf = netif_get_out (netif, -1);
    if (pktbuf) {
        net_err_t err = netif_put_in (netif, pktbuf, -1);
        if (err < 0) {
            pktbuf_free (pktbuf);
            return err;
        }

    }
    return NET_ERR_OK;
}

static const netif_ops_t loop_ops = {
    .close = loop_close,
    .open = loop_open,
    .xmit = loop_xmit,
};

net_err_t loop_init (void) {
    dbg_info(DBG_NETIF, "loop init");

    netif_t * netif = netif_open("loop", &loop_ops, (void *)0);
    if (!netif) {
        dbg_error(DBG_NETIF, "loop init failed!");
        return NET_ERR_NONE;
    }

    ipaddr_t ip, mask;
    ipaddr_from_str(&ip, "127.0.0.1");
    ipaddr_from_str(&mask, "255.0.0.0");

    netif_set_addr(netif, &ip, &mask, (ipaddr_t *)0);

    netif_set_active(netif);

    dbg_info(DBG_NETIF, "loop init done!");
    return NET_ERR_OK;
}