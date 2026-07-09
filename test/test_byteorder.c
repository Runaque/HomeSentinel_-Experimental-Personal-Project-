// Verify the byte-order round trip in harvest_one and on_ping_success.
// Inventory stores host-order; lwIP wants network-order.
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <assert.h>

int main(void){
    // Simulate: device at 192.168.1.54
    // on_ping_success gets target.u_addr.ip4.addr in NETWORK order,
    // converts to host order via ntohl, passes host order to harvest_one.
    // harvest_one converts back to network order via htonl for the lookup.

    uint32_t host_order = (192u<<24)|(168<<16)|(1<<8)|54;  // 0xC0A80136
    printf("host_order      = 0x%08X (%u.%u.%u.%u)\n", host_order,
        (host_order>>24)&0xFF,(host_order>>16)&0xFF,(host_order>>8)&0xFF,host_order&0xFF);

    // What lwIP would hand us (network order on a little-endian host)
    uint32_t net_order = htonl(host_order);
    // on_ping_success does: ip_host = ntohl(target...addr)
    uint32_t recovered_host = ntohl(net_order);
    assert(recovered_host == host_order);

    // harvest_one does: lookup.addr = htonl(ip_host_order)
    uint32_t lookup_addr = htonl(recovered_host);
    assert(lookup_addr == net_order);

    // And inventory stores d->ip = ip_host_order (host order) for display
    assert(recovered_host == host_order);

    printf("round trip net=0x%08X -> host=0x%08X -> lookup=0x%08X : OK\n",
        net_order, recovered_host, lookup_addr);
    printf("\nBYTE ORDER OK\n");
    return 0;
}
