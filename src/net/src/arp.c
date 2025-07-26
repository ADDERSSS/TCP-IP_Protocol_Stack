#include "arp.h"
#include "net_cfg.h"
#include "dbg.h"
#include "mblock.h"

static arp_entry_t cache_tbl[ARP_CACHE_SIZE];
static mblock_t cache_block;
static nlist_t cache_list;

static net_err_t cache_init (void) {
    nlist_init(&cache_list);

    plat_memset(cache_tbl, 0, sizeof(cache_tbl));

    net_err_t err = mblock_init(&cache_block, cache_tbl, sizeof(arp_entry_t), ARP_CACHE_SIZE, NLOCKER_NONE);
    if (err < 0) {
        dbg_error(DBG_ARP, "mblock_init failed");
        return err;
    }

    return NET_ERR_OK;
}

net_err_t arp_init (void) {
    net_err_t err = cache_init();
    if (err < 0) {
        dbg_error(DBG_ARP, "cache init failed");
        return err;
    }

    return NET_ERR_OK;
}