#ifndef NLIST_H
#define NLIST_H

typedef struct _nlist_node_t {
    struct _nlist_node_t * pre;
    struct _nlist_node_t * next;
}nlist_node_t;

static inline void nlist_node_init (nlist_node_t * node) {
    node->next = node->pre = (nlist_node_t *)0;
}

static inline nlist_node_t * nlist_node_next (nlist_node_t * node) {
    return node->next;
}

static inline nlist_node_t * nlist_node_pre (nlist_node_t * node) {
    return node->pre;   
}

static inline void nlist_node_set_next (nlist_node_t * node, nlist_node_t * next) {
    node->next = next;
    next->pre = node;
}


typedef struct _nlist_t {
    nlist_node_t * first;
    nlist_node_t * last;
    int count;
}nlist_t;

void nlist_init (nlist_t * list);

static inline int nlist_count (nlist_t * list) {
    return list->count;
}

static inline int nlist_is_empty (nlist_t * list) {
    return list->count == 0;
}

static inline nlist_node_t * nlist_first (nlist_t * list) {
    return list->first;
}

static inline nlist_node_t * nlist_last (nlist_t * list) {
    return list->last;
}

void nlist_insert_first (nlist_t * list, nlist_node_t * node);

#define nlist_for_each(node, list) \
    for ((node) = nlist_first(list); (node); (node) = nlist_node_next(node))

#define noffset_in_parent(parent_type, member) ((char *)&((parent_type *)0)->member)

#define nlist_to_parent(node, parent_type, member) \
    (parent_type *)((char *)node - noffset_in_parent(parent_type, member))

#define nlist_entry(node, type, member) \
    ((node) ? nlist_to_parent((node), type, member) : (type *)0)

nlist_node_t * nlist_remove (nlist_t * list, nlist_node_t * node);

static inline nlist_node_t * nlist_remove_first (nlist_t * list) {
    return nlist_remove(list, nlist_first(list));
}

void nlist_insert_last (nlist_t * list, nlist_node_t * node);

static inline nlist_node_t * nlist_remove_last (nlist_t * list) {
    return nlist_remove(list, nlist_last(list));
}

void nlist_insert_after (nlist_t * list, nlist_node_t * pre, nlist_node_t * node);

#endif