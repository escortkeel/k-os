#include "lib/int.h"
#include "init/initcall.h"
#include "sync/spinlock.h"
#include "net/socket.h"
#include "net/ip/af_inet.h"
#include "net/ip/ip.h"
#include "net/ip/tcp.h"
#include "net/ip/udp.h"
#include "net/ip/icmp.h"
#include "net/ip/raw.h"

static list_head_t types[SOCK_MAX];
static DEFINE_SPINLOCK(type_lock);

void register_inet_protocol(inet_protocol_t *protocol) {
    uint32_t flags;
    spin_lock_irqsave(&type_lock, &flags);

    list_add_before(&protocol->list, &types[protocol->impl->type]);

    spin_unlock_irqstore(&type_lock, flags);
}

static sock_protocol_t * inet_find(uint32_t type, uint32_t protocol) {
    bool success;
    inet_protocol_t *found;
    LIST_FOR_EACH_ENTRY(found, &types[type], list) {
        success = true;
        if(found->protocol == protocol || found->protocol == IPPROTO_RAW) {
            break;
        } else if(protocol == IPPROTO_RAW) {
            protocol = found->protocol;
            break;
        }
        success = false;
    }

    if(!success) return NULL;

    return found->impl;
}

sock_family_t af_inet = {
    .family    = AF_INET,
    .addr_len  = sizeof(ip_and_port_t),
    .find      = inet_find,
};

static inet_protocol_t builtin_protocols[] = {
    {
        .protocol = IP_PROT_TCP,
        .impl     = &tcp_protocol
    },
    {
        .protocol = IP_PROT_UDP,
        .impl     = &udp_protocol
    },
    {
        .protocol = IP_PROT_ICMP,
        .impl     = &icmp_protocol
    },
    {
        .protocol = 0,
        .impl     = &raw_protocol
    },
};

static INITCALL inet_init() {
    for(uint32_t i = 0; i < SOCK_MAX; i++) {
        list_init(&types[i]);
    }

    for(uint32_t i = 0; i < sizeof(builtin_protocols) / sizeof(inet_protocol_t); i++) {
        register_inet_protocol(&builtin_protocols[i]);
    }

    register_sock_family(&af_inet);

    return 0;
}

core_initcall(inet_init);
