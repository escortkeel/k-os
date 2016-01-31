#ifndef KERNEL_COMMON_LISTENER_H
#define KERNEL_COMMON_LISTENER_H

#include "common/types.h"
#include "common/list.h"

typedef chain_head_t listener_chain_head_t;

typedef struct listener listener_t;

typedef void (*callback_t)(listener_t *listener, uint32_t change, void *data);

struct listener {
    callback_t callback;

    chain_node_t node;
};

#define listener_chain_init(head)                                               \
    chain_init(head)

#define listener_chain_add(listener, head)                                      \
    chain_add_head(&(listener)->node, head)

#define listener_chain_rm(listener)                                             \
    chain_rm(&(listener)->node)

static inline void listener_chain_fire(uint32_t change, void *data, listener_chain_head_t *chain) {
    listener_t *listener;
    CHAIN_FOR_EACH_ENTRY(listener, chain, node) {
        listener->callback(listener, change, data);
    };
}

#define LISTENER_CHAIN_HEAD CHAIN_HEAD
#define DEFINE_LISTENER_CHAIN(name) listener_chain_head_t name = LISTENER_CHAIN_HEAD
#define DEFINE_LISTENER_CHAIN_ARRAY(name, num) listener_chain_head_t name[num] = { [0 ... (num - 1)] = LISTENER_CHAIN_HEAD };

#endif
