#include "timer.h"
#include "dbg.h"
#include "sys_plat.h"

static nlist_t timer_list;

#if DBG_DISP_ENABLE(DBG_TIMER)
static void display_timer_list (void) {
    plat_printf("-----------------timer list------------------\n");
    nlist_node_t * node;
    int index = 0;
    nlist_for_each(node, &timer_list) {
        net_timer_t * timer = nlist_entry(node, net_timer_t, node);
        plat_printf("[%d] timer name: %s, period: %d, curr: %d ms, reload: %d ms\n", index++, timer->name, (timer->flags & NET_TIMER_RELOAD ? 1 : 0), timer->curr, timer->reload);
    }
    plat_printf("---------------------------------------------\n");
}
#else
#define display_timer_list()
#endif

net_err_t net_timer_init (void) {
    dbg_info(DBG_TIMER, "net timer init\n");

    nlist_init(&timer_list);

    dbg_info(DBG_TIMER, "net timer init done!\n");
    return NET_ERR_OK;
}

static void insert_timer (net_timer_t * insert) { 
    nlist_node_t * node;
    nlist_for_each(node, &timer_list) {
        net_timer_t * curr = nlist_entry(node, net_timer_t, node);

        if (insert->curr > curr->curr) {
            insert->curr -= curr->curr;
        } else if (insert->curr == curr->curr) { 
            insert->curr = 0;
            nlist_insert_after(&timer_list, node, &insert->node);
            return;
        } else {
            curr->curr -= insert->curr;

            nlist_node_t * pre = nlist_node_pre(node);
            if (pre) {
                nlist_insert_after(&timer_list, pre, &insert->node);
            } else {
                nlist_insert_first(&timer_list, &insert->node);
            }
            return;
        }
    }

    nlist_insert_last(&timer_list, &insert->node);
    return;
}

net_err_t net_timer_add (net_timer_t * timer, const char * name, timer_proc_t proc, void * args, int ms, int flags) {
    dbg_info(DBG_TIMER, "insert timer %s\n", name);

    plat_strncpy(timer->name, name, TIMER_NAME_SIZE);
    timer->name[TIMER_NAME_SIZE - 1] = '\0';

    timer->proc = proc;
    timer->reload = ms;
    timer->curr = ms;
    timer->args = args;
    timer->flags = flags;

    insert_timer(timer);

    display_timer_list();

    return NET_ERR_OK;
}

void net_timer_remove (net_timer_t * timer) {
    dbg_info(DBG_TIMER, "remove timer %s\n", timer->name);

    nlist_node_t * node;
    nlist_for_each(node, &timer_list) {
        net_timer_t * t = nlist_entry(node, net_timer_t, node);
        if (timer != t) {
            continue;
        }

        nlist_node_t * next = nlist_node_next(&timer->node);
        if (next) {
            net_timer_t * next_timer = nlist_entry(next, net_timer_t, node);
            next_timer->curr += timer->curr;
        }

        nlist_remove(&timer_list, node);
        break;
    }

    display_timer_list();
}

net_err_t net_timer_check_tmo (int diff_ms) {
    nlist_t wait_list;
    nlist_init(&wait_list);

    nlist_node_t * node = nlist_first(&timer_list);
    while (node) {
        nlist_node_t * next = nlist_node_next(node);
        net_timer_t * timer = nlist_entry(node, net_timer_t, node);

        if (timer->curr > diff_ms) {
            timer->curr -= diff_ms;
            break;
        } 

        diff_ms -= timer->curr;
        timer->curr = 0;

        nlist_remove(&timer_list, node);
        nlist_insert_last(&wait_list, node);

        node = next;
    }

    while ((node = nlist_remove_first(&wait_list)) != (nlist_node_t *)0) {
        net_timer_t * timer = nlist_entry(node, net_timer_t, node);

        timer->proc(timer, timer->args);

        if (timer->flags & NET_TIMER_RELOAD) {
            timer->curr = timer->reload;
            insert_timer(timer);
        }
    }

    display_timer_list();
    return NET_ERR_OK;
}

int net_timer_first_tmo (void) {
    nlist_node_t * node = nlist_first(&timer_list);
    if (node) {
        net_timer_t * timer = nlist_entry(node, net_timer_t, node);
        return timer->curr;
    }

    return 0;
}