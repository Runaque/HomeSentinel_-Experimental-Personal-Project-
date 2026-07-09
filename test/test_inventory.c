// Host-side test of the inventory ring buffer + device table logic.
// We copy the pure algorithms (no ESP deps) and verify behaviour.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define INV_MAX_DEVICES 256
#define INV_EVENT_RING_SIZE 512

typedef struct { int64_t ts; int id; uint8_t mac[6]; } event_t;
static event_t s_events[INV_EVENT_RING_SIZE];
static size_t s_event_head, s_event_count;

static void log_event(int id){
    event_t *e=&s_events[s_event_head];
    memset(e,0,sizeof(*e)); e->id=id; e->ts=id;
    s_event_head=(s_event_head+1)%INV_EVENT_RING_SIZE;
    if(s_event_count<INV_EVENT_RING_SIZE) s_event_count++;
}
static size_t get_events(event_t *out,size_t max){
    size_t n=(max<s_event_count)?max:s_event_count;
    for(size_t i=0;i<n;i++){
        size_t pos=(s_event_head+INV_EVENT_RING_SIZE-1-i)%INV_EVENT_RING_SIZE;
        out[i]=s_events[pos];
    }
    return n;
}

// randomized MAC bit test
static bool mac_is_randomized(const uint8_t mac[6]){ return (mac[0]&0x02)!=0; }

int main(void){
    event_t buf[INV_EVENT_RING_SIZE];

    // Test 1: empty ring returns 0
    assert(get_events(buf,10)==0);
    printf("T1 empty ring: ok\n");

    // Test 2: partial fill, newest-first ordering
    for(int i=1;i<=5;i++) log_event(i);
    size_t n=get_events(buf,10);
    assert(n==5);
    assert(buf[0].id==5 && buf[4].id==1);  // newest first
    printf("T2 partial fill newest-first: ok (got %zu, head=%d tail=%d)\n",
           n, buf[0].id, buf[4].id);

    // Test 3: max cap on request
    n=get_events(buf,3);
    assert(n==3 && buf[0].id==5 && buf[2].id==3);
    printf("T3 request cap: ok\n");

    // Test 4: overflow wraparound — push way past ring size
    memset(&s_events,0,sizeof(s_events)); s_event_head=0; s_event_count=0;
    for(int i=1;i<=INV_EVENT_RING_SIZE+200;i++) log_event(i);
    assert(s_event_count==INV_EVENT_RING_SIZE);  // capped
    n=get_events(buf,INV_EVENT_RING_SIZE);
    assert(n==INV_EVENT_RING_SIZE);
    // newest should be the last one logged
    assert(buf[0].id==INV_EVENT_RING_SIZE+200);
    // oldest retained should be (total - ringsize + 1)
    int expected_oldest=(INV_EVENT_RING_SIZE+200)-INV_EVENT_RING_SIZE+1;
    assert(buf[INV_EVENT_RING_SIZE-1].id==expected_oldest);
    printf("T4 overflow wrap: ok (newest=%d oldest=%d expected_oldest=%d)\n",
           buf[0].id, buf[INV_EVENT_RING_SIZE-1].id, expected_oldest);

    // Test 5: randomized MAC detection
    uint8_t global_mac[6]={0x3C,0x22,0xFB,0x11,0x22,0x33}; // Apple OUI, bit1 clear
    uint8_t random_mac[6]={0x6A,0x4D,0x2B,0x11,0x22,0x33}; // 0x6A=0110 1010, bit1 set
    assert(mac_is_randomized(global_mac)==false);
    assert(mac_is_randomized(random_mac)==true);
    printf("T5 randomized-MAC bit: ok (global=%d random=%d)\n",
           mac_is_randomized(global_mac), mac_is_randomized(random_mac));

    printf("\nALL TESTS PASSED\n");
    return 0;
}
