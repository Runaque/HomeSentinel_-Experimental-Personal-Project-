#include <stdio.h>
#include <stdint.h>
#include <assert.h>

// Mirror the dedup logic from on_scan_complete.
// Events are stored newest-first in buf[0..n). We forward those with
// ts > last_notified, oldest-first, and advance the watermark.
typedef struct { int64_t ts; int id; } event_t;

static int forwarded[64], nfwd;
static int64_t last_notified;

static void run(event_t *buf, size_t n){
    nfwd=0;
    int64_t newest=last_notified;
    for(size_t i=n;i>0;i--){
        event_t *e=&buf[i-1];
        if(e->ts > last_notified){
            forwarded[nfwd++]=e->id;
            if(e->ts>newest) newest=e->ts;
        }
    }
    last_notified=newest;
}

int main(void){
    last_notified=0;

    // Pass 1: three events at ts 10,20,30 (stored newest-first)
    event_t p1[]={{30,3},{20,2},{10,1}};
    run(p1,3);
    assert(nfwd==3);
    assert(forwarded[0]==1 && forwarded[1]==2 && forwarded[2]==3); // chronological
    assert(last_notified==30);
    printf("P1: forwarded 3 in order, watermark=30\n");

    // Pass 2: same events still in ring + one new at ts 40. Only 40 forwards.
    event_t p2[]={{40,4},{30,3},{20,2},{10,1}};
    run(p2,4);
    assert(nfwd==1 && forwarded[0]==4);
    assert(last_notified==40);
    printf("P2: only new event (id4) forwarded, no re-spam\n");

    // Pass 3: no new events at all
    event_t p3[]={{40,4},{30,3},{20,2},{10,1}};
    run(p3,4);
    assert(nfwd==0);
    assert(last_notified==40);
    printf("P3: nothing new, nothing forwarded\n");

    // Pass 4: two new (50,60), ring dropped the oldest
    event_t p4[]={{60,6},{50,5},{40,4},{30,3}};
    run(p4,4);
    assert(nfwd==2 && forwarded[0]==5 && forwarded[1]==6);
    assert(last_notified==60);
    printf("P4: two new (id5,id6) in order, watermark=60\n");

    printf("\nNOTIFY DEDUP OK\n");
    return 0;
}
