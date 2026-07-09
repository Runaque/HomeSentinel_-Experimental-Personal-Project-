#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// --- verbatim pure helpers from anomaly.c ---
static bool is_night(uint8_t hour, uint8_t start, uint8_t end){
    if(start==end) return false;
    if(start<end) return (hour>=start && hour<end);
    return (hour>=start || hour<end);
}
static bool is_growth(uint32_t baseline, uint32_t current, uint8_t pct){
    if(baseline==0) return false;
    if(current<=baseline) return false;
    return ((uint64_t)current*100) >= ((uint64_t)baseline*(100+pct));
}

// --- simplified state machine mirroring the diff loop edges ---
typedef struct { int announced, online, was_online; } dev;
typedef enum { E_NONE, E_NEW, E_MISSING, E_RETURNED } ev;

static ev step(dev *d, int alert_randomized, int randomized){
    if(!d->announced && d->online){
        ev e=E_NONE;
        int announce = !(randomized && !alert_randomized);
        if(announce) e=E_NEW;
        d->announced=1; d->was_online=d->online;
        return e;
    }
    ev e=E_NONE;
    if(d->was_online && !d->online) e=E_MISSING;
    else if(!d->was_online && d->online) e=E_RETURNED;
    d->was_online=d->online;
    return e;
}

int main(void){
    int p=0,f=0;
    #define CHECK(c) do{ if(c){p++;} else {f++; printf("FAIL line %d\n",__LINE__);} }while(0)

    // --- night window: same-day 0..6 ---
    CHECK(is_night(0,0,6)==true);
    CHECK(is_night(3,0,6)==true);
    CHECK(is_night(6,0,6)==false);   // exclusive end
    CHECK(is_night(12,0,6)==false);

    // --- night window: wrap 22..6 ---
    CHECK(is_night(23,22,6)==true);
    CHECK(is_night(2,22,6)==true);
    CHECK(is_night(22,22,6)==true);  // inclusive start
    CHECK(is_night(6,22,6)==false);  // exclusive end
    CHECK(is_night(12,22,6)==false);

    // --- empty window ---
    CHECK(is_night(5,8,8)==false);

    // --- growth: +50% ---
    CHECK(is_growth(10,15,50)==true);   // exactly +50%
    CHECK(is_growth(10,14,50)==false);  // +40%
    CHECK(is_growth(10,16,50)==true);   // +60%
    CHECK(is_growth(14,29,50)==true);   // the spec example (107%)
    CHECK(is_growth(0,5,50)==false);    // no baseline
    CHECK(is_growth(10,10,50)==false);  // no change
    CHECK(is_growth(10,9,50)==false);   // shrank

    // --- state machine: new device, normal MAC ---
    dev d={0}; d.online=1;
    CHECK(step(&d,0,0)==E_NEW);
    CHECK(step(&d,0,0)==E_NONE);   // already announced, still online
    // goes offline
    d.online=0;
    CHECK(step(&d,0,0)==E_MISSING);
    CHECK(step(&d,0,0)==E_NONE);   // stays offline, silent
    // comes back
    d.online=1;
    CHECK(step(&d,0,0)==E_RETURNED);
    CHECK(step(&d,0,0)==E_NONE);   // stays online, silent

    // --- randomized MAC suppressed: no NEW event, but marked announced ---
    dev r={0}; r.online=1;
    CHECK(step(&r,0,1)==E_NONE);   // suppressed (alert_randomized=0)
    CHECK(r.announced==1);          // still marked so we don't re-eval
    // but if it later goes missing, we stay silent because was_online was set
    r.online=0;
    CHECK(step(&r,0,1)==E_MISSING); // it WAS online, now isn't -> missing fires
    // Note: this is acceptable; a suppressed-new randomized device going away
    // is a low-severity missing event, not a false "new" storm.

    // --- randomized MAC WITH alerting enabled ---
    dev r2={0}; r2.online=1;
    CHECK(step(&r2,1,1)==E_NEW);   // alert_randomized=1 -> fires

    printf("\n%d passed, %d failed\n", p, f);
    return f?1:0;
}
