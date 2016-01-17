#include "lib/string.h"
#include "lib/rand.h"
#include "common/swap.h"
#include "init/initcall.h"
#include "common/listener.h"
#include "mm/cache.h"
#include "net/packet.h"
#include "net/interface.h"
#include "net/eth/eth.h"
#include "net/ip/ip.h"
#include "net/ip/udp.h"
#include "net/ip/dhcp.h"
#include "video/log.h"

#define END_PADDING 28

#define OP_REQUEST 1
#define OP_REPLY   2

#define MAGIC_COOKIE 0x63825363

#define OPT_PAD               0
#define OPT_SUBNET_MASK       1
#define OPT_ROUTER            3
#define OPT_DNS               6
#define OPT_HOST_NAME         12
#define OPT_REQUESTED_IP_ADDR 50
#define OPT_LEASE_TIME        51
#define OPT_MESSAGE_TYPE      53
#define OPT_SERVER_ID         54
#define OPT_PARAMETER_REQUEST 55
#define OPT_RENEWAL_TIME      58
#define OPT_REBINDING_TIME    59
#define OPT_END               255

#define MSG_DISCOVER 1
#define MSG_OFFER    2
#define MSG_REQUEST  3
#define MSG_DECLINE  4
#define MSG_ACK      5
#define MSG_NAK      6
#define MSG_RELEASE  7
#define MSG_INFORM   8

typedef struct dhcp_header {
    uint8_t  op;        //Op code
    uint8_t  htype;     //Hardware address type
    uint8_t  hlen;      //Hardware address length
    uint8_t  hops;      //Private relay agent data
    uint32_t xid;       //Random transaction id
    uint16_t secs;      //Seconds elapsed
    uint16_t flags;     //Flags
    ip_t     ciaddr;    //Client IP address
    ip_t     yiaddr;    //Your IP address
    ip_t     siaddr;    //Server IP address
    ip_t     giaddr;    //Relay agent IP address
    mac_t    chaddr;    //Client MAC address
    uint8_t  zero[10];  //Zero
    char     sname[64]; //Server host name
    char     file[128]; //Boot file name
    uint32_t cookie;    //Magic Cookie
} PACKED dhcp_header_t;

typedef struct dhcp_options {
    ip_t *subnet_mask;
    ip_t *router_list;
    uint32_t router_len;
    ip_t *dns_list;
    uint32_t dns_len;
    ip_t *requested_ip;
    uint32_t lease;
    uint32_t message_type;
    ip_t *server;
    uint8_t *param_list;
    uint32_t param_len;
} dhcp_options_t;

static bool dhcp_parse_options(dhcp_options_t *opts, void *packet, uint16_t len) {
    uint8_t *ptr = packet;
    uint8_t *end = ptr + len;

    memset(opts, 0, sizeof(dhcp_options_t));

    while(ptr < end) {
        uint8_t opt = *ptr++;

        if(opt == OPT_PAD) continue;
        if(opt == OPT_END) break;

        if(ptr + 1 >= end) return false;
        uint8_t opt_len = *ptr++;
        if((ptr + opt_len + 1) >= end) return false;

        switch(opt) {
            case OPT_MESSAGE_TYPE: {
                opts->message_type = *ptr++;
                break;
            }
            case OPT_SUBNET_MASK: {
                if(opt_len < sizeof(ip_t)) return false;
                opts->subnet_mask = (ip_t *) ptr;
                ptr += sizeof(ip_t);
                break;
            }
            case OPT_ROUTER: {
                if(opt_len < sizeof(ip_t)) return false;
                opts->router_list = (ip_t *) ptr;
                opts->router_len = opt_len;
                ptr += sizeof(ip_t);
                break;
            }
            case OPT_DNS: {
                if(opt_len < sizeof(ip_t)) return false;
                opts->dns_list = (ip_t *) ptr;
                opts->dns_len = opt_len;
                ptr += sizeof(ip_t);
                break;
            }
            case OPT_REQUESTED_IP_ADDR: {
                if(opt_len < sizeof(ip_t)) return false;
                opts->requested_ip = (ip_t *) ptr;
                ptr += sizeof(ip_t);
                break;
            }
            case OPT_SERVER_ID: {
                if(opt_len < sizeof(ip_t)) return false;
                opts->server = (ip_t *) ptr;
                ptr += sizeof(ip_t);
                break;
            }
            case OPT_LEASE_TIME: {
                if(opt_len < sizeof(uint32_t)) return false;
                opts->lease = swap_uint32(*((uint32_t *) ptr));
                ptr += sizeof(uint32_t);
                break;
            }
            case OPT_PARAMETER_REQUEST: {
                opts->param_list = ptr++;
                opts->param_len = opt_len;
                break;
            }
            case OPT_RENEWAL_TIME: {
                ptr += opt_len;
                break;
            }
            case OPT_REBINDING_TIME: {
                ptr += opt_len;
                break;
            }
            default: {
                kprintf("dhcp - unknown option: %u", opt);
                break;
            }
        }
    }

    return true;
}

static void * dhcp_build_packet(dhcp_header_t *dhcp, mac_t addr, uint32_t xid) {
    memset(dhcp, 0, sizeof(dhcp_header_t));

    dhcp->op = OP_REQUEST;
    dhcp->htype = HTYPE_ETH;
    dhcp->hlen = sizeof(mac_t);
    dhcp->xid = xid;
    dhcp->chaddr = addr;
    dhcp->cookie = swap_uint32(MAGIC_COOKIE);

    return dhcp + 1;
}

#define OPTIONS_LEN_DISCOVER 9

static void dhcp_send_discover(net_interface_t *interface) {
    const char *hostname = net_get_hostname();
    uint32_t hostname_len = strlen(hostname);
    void *dhcp = kmalloc(sizeof(dhcp_header_t) + OPTIONS_LEN_DISCOVER + hostname_len + END_PADDING);

    uint8_t *ptr = dhcp_build_packet(dhcp, *((mac_t *) interface->hard_addr.addr), rand32());

    *ptr++ = OPT_MESSAGE_TYPE;
    *ptr++ = 1;
    *ptr++ = MSG_DISCOVER;

    *ptr++ = OPT_HOST_NAME;
    *ptr++ = hostname_len;
    memcpy(ptr, hostname, hostname_len);
    ptr += hostname_len;

    *ptr++ = OPT_PARAMETER_REQUEST;
    *ptr++ = 3;
    *ptr++ = OPT_SUBNET_MASK;
    *ptr++ = OPT_ROUTER;
    *ptr++ = OPT_DNS;

    *ptr++ = OPT_END;

    packet_t *packet = packet_create(interface, NULL, NULL, dhcp, sizeof(dhcp_header_t) + OPTIONS_LEN_DISCOVER + END_PADDING);
    udp_build(packet, IP_BROADCAST, swap_uint16(DHCP_PORT_CLIENT), swap_uint16(DHCP_PORT_SERVER));
    packet_send(packet);
}

#define OPTIONS_LEN_REQUEST 22

static void dhcp_send_request(net_interface_t *interface, dhcp_header_t *hdr, dhcp_options_t *opts) {
    const char *hostname = net_get_hostname();
    uint32_t hostname_len = strlen(hostname);
    void *dhcp = kmalloc(sizeof(dhcp_header_t) + OPTIONS_LEN_REQUEST + hostname_len + END_PADDING);

    uint8_t *ptr = dhcp_build_packet(dhcp, *((mac_t *) interface->hard_addr.addr), rand32());

    *ptr++ = OPT_MESSAGE_TYPE;
    *ptr++ = 1;
    *ptr++ = MSG_REQUEST;

    *ptr++ = OPT_HOST_NAME;
    *ptr++ = hostname_len;
    memcpy(ptr, hostname, hostname_len);
    ptr += hostname_len;

    *ptr++ = OPT_SERVER_ID;
    *ptr++ = sizeof(ip_t);
    *((ip_t *) ptr) = *opts->server;
    ptr += sizeof(ip_t);

    *ptr++ = OPT_REQUESTED_IP_ADDR;
    *ptr++ = sizeof(ip_t);
    *((ip_t *) ptr) = hdr->yiaddr;
    ptr += sizeof(ip_t);

    *ptr++ = OPT_PARAMETER_REQUEST;
    *ptr++ = 3;
    *ptr++ = OPT_SUBNET_MASK;
    *ptr++ = OPT_ROUTER;
    *ptr++ = OPT_DNS;

    *ptr++ = OPT_END;

    packet_t *packet = packet_create(interface, NULL, NULL, dhcp, sizeof(dhcp_header_t) + sizeof(opts) + END_PADDING);
    udp_build(packet, IP_BROADCAST, swap_uint16(DHCP_PORT_CLIENT), swap_uint16(DHCP_PORT_SERVER));
    packet_send(packet);
}

static void dhcp_ack(net_interface_t *interface, dhcp_header_t *hdr) {
    ((ip_interface_t *) interface->ip_data)->ip_addr = hdr->yiaddr;

    kprintf("dhcp - ip address ACK (%u.%u.%u.%u)",
        hdr->yiaddr.addr[0], hdr->yiaddr.addr[1],
        hdr->yiaddr.addr[2], hdr->yiaddr.addr[3]);

    net_set_state(interface, IF_READY);
}

void dhcp_handle(packet_t *packet, void *raw, uint16_t len) {
    if(len < sizeof(dhcp_header_t)) return;

    dhcp_header_t *dhcp = raw;
    raw = dhcp + 1;
    len -= sizeof(dhcp_header_t);

    if(dhcp->op != OP_REPLY) return;
    if(dhcp->htype != HTYPE_ETH) return;
    if(dhcp->hlen != sizeof(mac_t)) return;
    if(dhcp->cookie != swap_uint32(MAGIC_COOKIE)) return;
    if(memcmp(packet->interface->hard_addr.addr, &dhcp->chaddr, sizeof(mac_t))) return;

    dhcp_options_t opts;
    if(!dhcp_parse_options(&opts, raw, len)) return;

    switch (opts.message_type) {
        case MSG_OFFER: {
            dhcp_send_request(packet->interface, dhcp, &opts);
            break;
        }
        case MSG_ACK: {
            dhcp_ack(packet->interface, dhcp);
            break;
        }
        default: {
            kprintf("dhcp - unknown message type %u", opts.message_type);
            break;
        }
    }
}

static void dhcp_callback(listener_t *listener, net_state_t state, net_interface_t *interface) {
    switch(state) {
        case IF_UP: {
            dhcp_send_discover(interface);
            break;
        }
        default: break;
    };
}

static listener_t dhcp_listener = {
    .callback = (callback_t) dhcp_callback,
};

static INITCALL dhcp_init() {
    register_net_state_listener(&dhcp_listener);

    return 0;
}

core_initcall(dhcp_init);
