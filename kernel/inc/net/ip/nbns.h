#ifndef KERNEL_NET_IP_NBNS_H
#define KERNEL_NET_IP_NBNS_H

#include "common/types.h"
#include "common/compiler.h"
#include "net/packet.h"
#include "net/interface.h"

#define NBNS_PORT 137

void nbns_register_name(net_interface_t *interface, const char *name);
void nbns_handle(packet_t *packet, void *raw, uint16_t len);

#endif
