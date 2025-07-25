#include "pktbuf.h"
#include "dbg.h"
#include "mblock.h"
#include "nlocker.h"

static nlocker_t locker;
static pktblk_t block_buffer[PKTBUF_BLK_CNT];
static mblock_t block_list;
static pktbuf_t pktbuf_buffer[PKTBUF_BUF_CNT];
static mblock_t pktbuf_list;

static inline int total_blk_remain (pktbuf_t * buf) {
    return buf->total_size - buf->pos;
}

static inline int curr_blk_remain (pktbuf_t * buf) {
    pktblk_t * blk = buf->curr_blk;
    if (!blk) {
        return 0;
    }

    return (int)(blk->data + blk->size - buf->blk_offset);
}

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

    mblock_init(&block_list, block_buffer, sizeof(pktblk_t), PKTBUF_BLK_CNT, NLOCKER_NONE);
    mblock_init(&pktbuf_list, pktbuf_buffer, sizeof(pktbuf_t), PKTBUF_BUF_CNT, NLOCKER_NONE);

    dbg_info(DBG_BUF, "pktbuf init ok");
    return NET_ERR_OK;
}

static pktblk_t * pktblock_alloc(void) { 
    nlocker_lock(&locker);
    pktblk_t * block = (pktblk_t *)mblock_alloc(&block_list, -1);
    nlocker_unlock(&locker);
    if (block) {
        block->size = 0;
        block->data = (uint8_t *)0;
        nlist_node_init(&block->node);
    }

    return block;
}

static void pktblock_free(pktblk_t * block) { 
    nlocker_lock(&locker);
    mblock_free(&block_list, block);
    nlocker_unlock(&locker);
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

            if (first_block) {
                pktblk_free_list(first_block);
            }

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

void pktbuf_inc_ref(pktbuf_t * buf) {
    nlocker_lock(&locker);
    buf->ref++;
    nlocker_unlock(&locker);
}

pktbuf_t * pktbuf_alloc(int size) {
    nlocker_lock(&locker);
    pktbuf_t * buf = mblock_alloc(&pktbuf_list, -1);
    nlocker_unlock(&locker);
    if (!buf) {
        dbg_error(DBG_BUF, "pktbuf_alloc: no memory");
        return (pktbuf_t *)0;
    }

    buf->total_size = 0;
    buf->ref = 1;
    nlist_init(&buf->blk_list);
    nlist_node_init(&buf->node);

    if (size > 0) {
        pktblk_t * block = pktblock_alloc_list(size, 1);
        if (!block) {
            nlocker_lock(&locker);
            mblock_free(&pktbuf_list, buf);
            nlocker_unlock(&locker);
            return (pktbuf_t *)0;
        }

        pktbuf_insert_blk_list(buf, block, 1);
    }

    pktbuf_reset_acc(buf);
    display_check_buf(buf);

    return buf;
}

void pktbuf_free(pktbuf_t * pktbuf) {
    nlocker_lock(&locker);
    if (--pktbuf->ref == 0) {
        pktblk_free_list(pktbuf_first_blk(pktbuf));
        mblock_free(&pktbuf_list, pktbuf);
    }
    nlocker_unlock(&locker);
}

net_err_t pktbuf_add_header(pktbuf_t * buf, int size, int cont) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
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
    dbg_assert(buf->ref != 0, "buf->ref = 0");
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
    dbg_assert(buf->ref != 0, "buf->ref = 0");
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
    dbg_assert(dest->ref != 0, "buf->ref = 0");
    dbg_assert(src->ref != 0, "buf->ref = 0");
    pktblk_t * first;

    while ((first = pktbuf_first_blk(src))) {
        nlist_remove_first(&src->blk_list);
        pktbuf_insert_blk_list(dest, first, 1);
    }

    pktbuf_free(src);
    display_check_buf(dest);
    return NET_ERR_OK;
}

net_err_t pktbuf_set_cont(pktbuf_t * buf, int size) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (size > buf->total_size) {
        dbg_error(DBG_BUF, "pktbuf_set_cont: size %d > buf->total_size %d", size, buf->total_size);
        return NET_ERR_SIZE;
    }

    if (size > PKTBUF_BLK_SIZE) {
        dbg_error(DBG_BUF, "pktbuf_set_cont: size %d > PKTBUF_BLK_SIZE %d", size, PKTBUF_BLK_SIZE);
        return NET_ERR_SIZE;
    }

    pktblk_t * first_blk = pktbuf_first_blk(buf);
    if (size <= first_blk->size) {
        display_check_buf(buf);
        return NET_ERR_OK;
    }

    uint8_t * dest = first_blk->payload;
    for (int i = 0; i < first_blk->size; i++ ) {
        *dest++ = first_blk->data[i];
    }
    first_blk->data = first_blk->payload;

    pktblk_t * curr_blk = pktblk_blk_next(first_blk);
    int remain_size = size - first_blk->size;
    while (remain_size && curr_blk) {
        int curr_size = (curr_blk->size > remain_size) ? remain_size : curr_blk->size;

        plat_memcpy(dest, curr_blk->data, curr_size);
        dest += curr_size;
        curr_blk->data += curr_size;
        curr_blk->size -= curr_size;
        first_blk->size += curr_size;
        remain_size -= curr_size;
        
        if (curr_blk->size == 0) {
            pktblk_t * next_blk = pktblk_blk_next(curr_blk);
            nlist_remove(&buf->blk_list, &curr_blk->node);
            pktblock_free(curr_blk);

            curr_blk = next_blk;
        }

    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

void pktbuf_reset_acc(pktbuf_t * buf) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (buf) {
        buf->pos = 0;
        buf->curr_blk = pktbuf_first_blk(buf);
        buf->blk_offset = buf->curr_blk ? buf->curr_blk->data : (uint8_t *)0;
    }
}

static void move_forward(pktbuf_t * buf, int size) { 
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    buf->pos += size;
    buf->blk_offset += size;

    pktblk_t * curr_blk = buf->curr_blk;
    if (buf->blk_offset >= curr_blk->data + curr_blk->size) {
        buf->curr_blk = pktblk_blk_next(curr_blk);
        if (buf->curr_blk) {
            buf->blk_offset = buf->curr_blk->data;
        } else {
            buf->blk_offset = (uint8_t *)0;
        }
    }
}

net_err_t pktbuf_write(pktbuf_t * buf, uint8_t * src, int size) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (!src || !size) {
        return NET_ERR_PARAM;
    }

    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        dbg_error(DBG_BUF, "pktbuf_write: no enough space, remain size: %d, write size: %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    while (size) {
        int blk_size = curr_blk_remain(buf);

        int curr_copy = size > blk_size ? blk_size : size;
        plat_memcpy(buf->blk_offset, src, curr_copy);

        src += curr_copy;
        size -= curr_copy;

        move_forward(buf, curr_copy);
    }

    return NET_ERR_OK;
}

net_err_t pktbuf_fill(pktbuf_t * buf, uint8_t value, int size) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (!size) {
        return NET_ERR_PARAM;
    }

    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        dbg_error(DBG_BUF, "pktbuf_fill: no enough space, remain size: %d, fill size: %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    while (size) {
        int blk_size = curr_blk_remain(buf);

        int curr_fill = size > blk_size ? blk_size : size;
        plat_memset(buf->blk_offset, value, curr_fill);

        size -= curr_fill;

        move_forward(buf, curr_fill);
    }

    return NET_ERR_OK;
}

net_err_t pktbuf_read (pktbuf_t * buf, uint8_t * dest, int size) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (!dest || !size) {
        return NET_ERR_PARAM;
    }

    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        dbg_error(DBG_BUF, "pktbuf_read: no enough data, remain size: %d, read size: %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    while (size) {
        int blk_size = curr_blk_remain(buf);

        int curr_copy = size > blk_size ? blk_size : size;
        plat_memcpy(dest, buf->blk_offset, curr_copy);

        dest += curr_copy;
        size -= curr_copy;

        move_forward(buf, curr_copy);
    }

    return NET_ERR_OK;
}

net_err_t pktbuf_seek(pktbuf_t * buf, int offset) {
    dbg_assert(buf->ref != 0, "buf->ref = 0");
    if (buf->pos == offset) {
        return NET_ERR_OK;
    }

    if (offset < 0 || offset > buf->total_size) {
        return NET_ERR_SIZE;
    }

    int move_bytes;
    if (offset < buf->pos) {
        buf->curr_blk = pktbuf_first_blk(buf);
        buf->blk_offset = buf->curr_blk->data;
        buf->pos = 0;

        move_bytes = offset;
    } else {
        move_bytes = offset - buf->pos;
    }

    while (move_bytes) {
        int remain_size = curr_blk_remain(buf);
        int curr_move = move_bytes > remain_size ? remain_size : move_bytes;
        
        move_forward(buf, curr_move);
        move_bytes -= curr_move;
    }

    return NET_ERR_OK;
}

net_err_t pktbuf_copy(pktbuf_t * dest, pktbuf_t * src, int size) {
    dbg_assert(dest->ref != 0, "buf->ref = 0");
    dbg_assert(src->ref != 0, "buf->ref = 0");
    if ((total_blk_remain(dest) < size) || (total_blk_remain(src) < size)) {
        return NET_ERR_SIZE;
    }

    while (size) {
        int dest_remain = curr_blk_remain(dest);
        int src_remain = curr_blk_remain(src);
        int copy_size = dest_remain > src_remain ? src_remain : dest_remain;

        copy_size = size > copy_size ? copy_size : size;

        plat_memcpy(dest->blk_offset, src->blk_offset, copy_size);

        move_forward(dest, copy_size);
        move_forward(src, copy_size);
        size -= copy_size;
    }

    return NET_ERR_OK;
}


