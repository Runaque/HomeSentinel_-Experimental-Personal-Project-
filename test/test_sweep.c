// Host test of the subnet enumeration + batch pacing arithmetic from
// discovery.c run_pass(). We're verifying the loop bounds and batch math,
// not the ESP calls.
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#define DISC_PING_BATCH 16

// Mirror the host-range computation from run_pass().
static void enumerate(uint32_t net, uint32_t mask, uint32_t self,
                      int *out_pinged, int *out_batches){
    uint32_t host_count=(~mask)&0xFFFFFFFFu;
    if(host_count>1022) host_count=1022;
    int pinged=0, batches=0, pending=0;
    for(uint32_t h=1;h<host_count;h++){
        uint32_t ip=net+h;
        if(ip==self) continue;
        pinged++; pending++;
        if(pending>=DISC_PING_BATCH){ batches++; pending=0; }
    }
    if(pending>0) batches++;
    *out_pinged=pinged; *out_batches=batches;
}

int main(void){
    // /24: 192.168.1.0/24, self=.10
    uint32_t net=(192u<<24)|(168<<16)|(1<<8)|0;
    uint32_t mask=0xFFFFFF00u;
    uint32_t self=net|10;
    int pinged,batches;
    enumerate(net,mask,self,&pinged,&batches);
    // host_count=256, loop h=1..255 => 254 candidate, minus self (.10 in range) = 253
    printf("/24: pinged=%d batches=%d\n", pinged, batches);
    assert(pinged==253);
    assert(batches==(253+15)/16);

    // popcount of mask = prefix length
    assert(__builtin_popcount(mask)==24);

    // /22 clamp test: mask 255.255.252.0 => ~mask=0x3FF=1023, clamps to 1022
    uint32_t mask22=0xFFFFFC00u;
    uint32_t net22=(10u<<24);
    uint32_t self22=net22|5;
    enumerate(net22,mask22,self22,&pinged,&batches);
    printf("/22: pinged=%d batches=%d (clamped host range)\n", pinged, batches);
    // host_count clamped to 1022, loop h=1..1021 => 1020 candidates minus self = 1019
    assert(pinged==1020);

    printf("\nSWEEP LOGIC OK\n");
    return 0;
}
