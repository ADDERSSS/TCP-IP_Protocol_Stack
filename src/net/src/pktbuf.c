#include "pktbuf.h"
#include "dbg.h"
#include "mblock.h"
#include "nlocker.h"

static nlocker_t locker;
static pktblk_t block_buffer[PKTBUF_BLK_CNT];
static mblock_t block_list;
static pktbuf_t pktbuf_buffer[PKTBUF_BUF_CNT];
static mblock_t pktbuf_list;

static inline int curr_blk_tail_free (pktblk_t * blk) {
    return (int)(blk->payload + PKTBUF_BLK_SIZE - (blk->data + blk->size));
}

#if DBG_DISP_ENABLE(DBG_BUF)
static void display_check_buf (pktbuf_t * buf) {
    if (!buf) {
        dbg_error(DBG_BUF, "pktbuf is null");
        return;
    }

    plat_printf("check buf %p: size=%d\n", buf, buf->total_size);
    pktblk_t * curr;
    int index = 0, total_size = 0;
    for (curr = pktbuf_first_blk(buf); curr; curr = pktblk_blk_next(curr)) {
        plat_printf("%d: ", index++);

        if (curr->data < curr->payload || (curr->data >= curr->payload + PKTBUF_BLK_SIZE)) {
            dbg_error(DBG_BUF, "pktblk %p data is out of range", curr);
        }

        int pre_size = (int)(curr->data - curr->payload);
        plat_printf("pre: %d b, ", pre_size);

        int used_size = curr->size;
        plat_printf("used: %d b, ", used_size);

        int free_size = curr_blk_tail_free(curr);
        plat_printf("free: %d b\n", free_size);

        int blk_total = pre_size + used_size + free_size;
        if (blk_total != PKTBUF_BLK_SIZE) {
            dbg_error(DBG_BUF, "bad blk size! < blk_total: %d != PKTBUF_BLK_SIZE: %d >\n", blk_total, PKTBUF_BLK_SIZE);
        }

        total_size += used_size;
    }
    
    if (total_size != buf->total_size) {
        dbg_error(DBG_BUF, "bad buf size! < total_size: %d != buf->total_size: %d >\n", total_size, buf->total_size);
    }
}
#else
#define display_check_buf(buf)
#endif

net_err_t pktbuf_init(void) {
    dbg_info(DBG_BUF, "pktbuf init");
    nlocker_init(&locker, NLOCKER_THREAD);

    mblock_init(&block_list, block_buffer, sizeof(pktblk_t), PKTBUF_BLK_CNT, NLOCKER_THREAD);
    mblock_init(&pktbuf_list, pktbuf_buffer, sizeof(pktbuf_t), PKTBUF_BUF_CNT, NLOCKER_THREAD);

    dbg_info(DBG_BUF, "pktbuf init ok");
    return NET_ERR_OK;
}

static pktblk_t * pktblock_alloc(void) { 
    pktblk_t * block = (pktblk_t *)mblock_alloc(&block_list, -1);
    if (block) {
        block->size = 0;
        block->data = (uint8_t *)0;
        nlist_node_init(&block->node);
    }

    return block;
}

static void pktblock_free(pktblk_t * block) { 
    mblock_free(&block_list, block);
}

static void pktblk_free_list (pktblk_t * first) {
    while (first) {
        pktblk_t * next = pktblk_blk_next(first);
        pktblock_free(first);
        first = next;
    }
}

static pktblk_t * pktblock_alloc_list(int size, int add_front) { 
    pktblk_t * first_block = (pktblk_t *)0;
    pktblk_t * pre_block = (pktblk_t *)0;

    while (size) {
        pktblk_t * new_block = pktblock_alloc();
        if (!new_block) {
            dbg_error(DBG_BUF, "pktblock_alloc_list(%d): no memory", size);

            pktblk_free_list(first_block);
            return (pktblk_t *)0;
        }

        int curr_size = 0;
        if (add_front) {
            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;
            new_block->size = curr_size;
            new_block->data = new_block->payload + PKTBUF_BLK_SIZE - curr_size;
            if (first_block) {
                nlist_node_set_next(&new_block->node, &first_block->node);
            }

            first_block = new_block;
        } else {
            if (!first_block) {
                first_block = new_block;
            }

            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;
            new_block->size = curr_size;
            new_block->data = new_block->payload;
            if (pre_block) {
                nlist_node_set_next(&pre_block->node, &new_block->node);
            }

        }

        size -= curr_size;
        pre_block = new_block;

    }

    return first_block;
}

static void pktbuf_insert_blk_list (pktbuf_t * buf, pktblk_t * first_blk, int add_last) {
    if (add_last) {
        while (first_blk) {
            pktblk_t * next_blk = pktblk_blk_next(first_blk);

            nlist_insert_last(&buf->blk_list, &first_blk->node);
            buf->total_size += first_blk->size;

            first_blk = next_blk;
        }
    } else {
        pktblk_t * pre = (pktblk_t *)0;
        while (first_blk) {
            pktblk_t * next_blk = pktblk_blk_next(first_blk);

            if (pre) {
                nlist_insert_after(&buf->blk_list, &pre->node, &first_blk->node);
            } else {
                nlist_insert_first(&buf->blk_list, &first_blk->node);
            }

            buf->total_size += first_blk->size;

            pre = first_blk;
            first_blk = next_blk;

        }
    }
}

pktbuf_t * pktbuf_alloc(int size) {
    pktbuf_t * buf = mblock_alloc(&pktbuf_list, -1);
    if (!buf) {
        dbg_error(DBG_BUF, "pktbuf_alloc: no memory");
        return (pktbuf_t *)0;
    }

    buf->total_size = 0;
    nlist_init(&buf->blk_list);
    nlist_node_init(&buf->node);

    if (size > 0) {
        pktblk_t * block = pktblock_alloc_list(size, 1);
        if (!block) {
            mblock_free(&pktbuf_list, buf);
            return (pktbuf_t *)0;
        }

        pktbuf_insert_blk_list(buf, block, 1);
    }

    display_check_buf(buf);

    return buf;
}

void pktbuf_free(pktbuf_t * pktbuf) {
    pktblk_free_list(pktbuf_first_blk(pktbuf));
    mblock_free(&pktbuf_list, pktbuf);
}

net_err_t pktbuf_add_header(pktbuf_t * buf, int size, int cont) {
    pktblk_t * block = pktbuf_first_blk(buf);

    int resv_size = (int)(block->data - block->payload);
    if (size <= resv_size) {
        block->size += size;
        block->data -= size;
        buf->total_size += size;

        display_check_buf(buf);
        return NET_ERR_OK;
    }

    if (cont) {
        if (size > PKTBUF_BLK_SIZE) {
            dbg_error(DBG_BUF, "set cont, size too big: %d > %d", size, PKTBUF_BLK_SIZE);
            return NET_ERR_SIZE;
        }

        block = pktblock_alloc_list(size, 1);
        if (!block) {
            dbg_error(DBG_BUF, "set cont, alloc block failed");
            return NET_ERR_NONE;
        }

    } else {
        block->data = block->payload;
        block->size += resv_size;
        buf->total_size += resv_size;
        size -= resv_size;

        block = pktblock_alloc_list(size, 1);
        if (!block) {
            dbg_error(DBG_BUF, "set cont, alloc block failed");
            return NET_ERR_NONE;
        }

    }

    pktbuf_insert_blk_list(buf, block, 0);
    display_check_buf(buf);
    return NET_ERR_OK;
}

net_err_t pktbuf_remove_header(pktbuf_t * buf, int size) {
    pktblk_t * block = pktbuf_first_blk(buf);
    while (size) {
        pktblk_t * next_block = pktblk_blk_next(block);

        if (size < block->size) {
            block->data += size;
            block->size -= size;
            buf->total_size -= size;
            break;
        }

        int curr_size = block->size;

        nlist_remove_first(&buf->blk_list);
        pktblock_free(block);

        size -= curr_size;
        buf->total_size -= curr_size;

        block = next_block;
    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

net_err_t pktbuf_resize (pktbuf_t * buf, int to_size) {
    if (to_size == buf->total_size) {
        return NET_ERR_OK;
    }

    if (buf->total_size == 0) {
        pktblk_t * block = pktblock_alloc_list(to_size, 0);
        if (!block) {
            dbg_error(DBG_BUF, "pktbuf_resize: no memory");
            return NET_ERR_MEM;
        }

        pktbuf_insert_blk_list(buf, block, 1);
    } else if (to_size == 0) {
        pktblk_free_list(pktbuf_first_blk(buf));
        buf->total_size = 0;
        nlist_init(&buf->blk_list);
    } else if (to_size > buf->total_size) {
        pktblk_t * tail_block = pktbuf_last_blk(buf);

        int inc_size = to_size - buf->total_size;
        int remain_size = curr_blk_tail_free(tail_block);

        if (remain_size >= inc_size) {
            tail_block->size += inc_size;
            buf->total_size += inc_size;
        } else {
            pktblk_t * new_blocks = pktblock_alloc_list(inc_size - remain_size, 0);
            if (!new_blocks) {
                dbg_error(DBG_BUF, "pktbuf_resize: no memory");
                return NET_ERR_MEM;
            }
            tail_block->size += remain_size;
            buf ->total_size += remain_size;
            pktbuf_insert_blk_list(buf, new_blocks, 1);
        }
    } else {
        int total_size = 0;

        pktblk_t * tail_block;
        for (tail_block = pktbuf_first_blk(buf); tail_block; tail_block = pktblk_blk_next(tail_block)) {
            total_size += tail_block->size;
            if (total_size >= to_size) {
                break;
            }
        }

        if (tail_block == (pktblk_t *)0) {
            return NET_ERR_SIZE;
        }

        total_size = 0;
        pktblk_t * curr_blk = pktblk_blk_next(tail_block);
        while (curr_blk) {
            pktblk_t * next_blk = pktblk_blk_next(curr_blk);

            total_size += curr_blk->size;
            nlist_remove(&buf->blk_list, &curr_blk->node);
            pktblock_free(curr_blk);

            curr_blk = next_blk;
        }

        tail_block->size -= buf->total_size - total_size - to_size;
        buf->total_size = to_size;

    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

net_err_t pktbuf_join (pktbuf_t * dest, pktbuf_t * src) {
    pktblk_t * first;

    while ((first = pktbuf_first_blk(src))) {
        nlist_remove_first(&src->blk_list);
        pktbuf_insert_blk_list(dest, first, 1);
    }

    pktbuf_free(src);
    display_check_buf(dest);
    return NET_ERR_OK;
}