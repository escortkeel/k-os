#include "lib/int.h"
#include "common/list.h"
#include "common/listener.h"
#include "sync/spinlock.h"
#include "net/types.h"
#include "net/protocols.h"
#include "net/interface.h"
#include "net/dhcp.h"
#include "net/nbns.h"
#include "video/log.h"

static char *hostname = "K-OS"; //TODO touppercase this when it gets dynamically loaded
static uint32_t hostname_handles;

static DEFINE_LIST(interfaces);
static DEFINE_LISTENER_CHAIN(listeners);
static DEFINE_SPINLOCK(interface_lock);
static DEFINE_SPINLOCK(listener_lock);

void register_net_interface(net_interface_t *interface, net_state_t state) {
    interface->ip = IP_NONE;

    interface->rx_total = 0;
    interface->tx_total = 0;

    uint32_t flags;
    spin_lock_irqsave(&interface_lock, &flags);

    list_add(&interface->list, &interfaces);

    spin_unlock_irqstore(&interface_lock, flags);

    net_set_state(interface, state);
}

void unregister_net_interface(net_interface_t *interface) {
    uint32_t flags;
    spin_lock_irqsave(&interface_lock, &flags);

    list_rm(&interface->list);

    spin_unlock_irqstore(&interface_lock, flags);
}

void register_net_event_listener(listener_t *listener) {
    uint32_t flags;
    spin_lock_irqsave(&listener_lock, &flags);

    listener_chain_add(listener, &listeners);

    spin_unlock_irqstore(&listener_lock, flags);
}

void unregister_net_event_listener(listener_t *listener) {
    uint32_t flags;
    spin_lock_irqsave(&listener_lock, &flags);

    listener_chain_rm(listener);

    spin_unlock_irqstore(&listener_lock, flags);
}

char * net_get_hostname() {
    uint32_t flags;
    spin_lock_irqsave(&interface_lock, &flags);

    hostname_handles++;
    char *local_name = ACCESS_ONCE(hostname);

    spin_unlock_irqstore(&interface_lock, flags);

    return local_name;
}

void net_put_hostname() {
    uint32_t flags;
    spin_lock_irqsave(&interface_lock, &flags);

    hostname_handles--;

    spin_unlock_irqstore(&interface_lock, flags);
}

void net_recieve(net_interface_t *interface, void *raw, uint16_t len) {
    packet_t packet;
    packet.interface = interface;
    interface->hard.recieve(&packet, raw, len);
}

void net_set_state(net_interface_t *interface, net_state_t state) {
    if(interface->state == state) return;

    //FIXME locking
    interface->state = state;

    //TODO better handle active devices, etc
    switch(state) {
        case IF_UP: {
            logf("net - interface is UP (%02X:%02X:%02X:%02X:%02X:%02X)",
                interface->mac.addr[0], interface->mac.addr[1],
                interface->mac.addr[2], interface->mac.addr[3],
                interface->mac.addr[4], interface->mac.addr[5]
            );

            dhcp_start(interface);

            break;
        }
        case IF_DOWN: {
            logf("net - interface is DOWN (%02X:%02X:%02X:%02X:%02X:%02X)",
                interface->mac.addr[0], interface->mac.addr[1],
                interface->mac.addr[2], interface->mac.addr[3],
                interface->mac.addr[4], interface->mac.addr[5]
            );
            break;
        }
        case IF_READY: {
            nbns_register_name(interface, net_get_hostname());
            net_put_hostname();

            logf("net - interface is READY (%u.%u.%u.%u)",
                interface->ip.addr[0], interface->ip.addr[1],
                interface->ip.addr[2], interface->ip.addr[3]
            );
            break;
        }
        case IF_ERROR: {
            logf("net - interface error");
            break;
        }
        default: break;
    }

    uint32_t flags;
    spin_lock_irqsave(&listener_lock, &flags);

    listener_chain_fire(state, interface, &listeners);

    spin_unlock_irqstore(&listener_lock, flags);
}
