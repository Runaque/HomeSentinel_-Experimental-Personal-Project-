// Host test of the mDNS apply-by-IP matching logic from apply_mdns_result.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define HOSTLEN 64
typedef enum { CLS_UNKNOWN=0, CLS_COMPUTER, CLS_MOBILE, CLS_IOT, CLS_INFRA } cls_t;
typedef struct { uint32_t ip; char hostname[HOSTLEN]; cls_t cls; int in_use; } dev_t;

#define N 8
static dev_t devs[N];

// mirror of the matching + fill logic (single IP, single result)
static void apply(uint32_t ip_host, const char *name, cls_t hint){
    for(int i=0;i<N;i++){
        if(devs[i].in_use && devs[i].ip==ip_host){
            if(devs[i].hostname[0]=='\0')
                strncpy(devs[i].hostname,name,HOSTLEN-1);
            if(devs[i].cls==CLS_UNKNOWN)
                devs[i].cls=hint;
            return;
        }
    }
}

int main(void){
    // three devices, one already named, one with a user class
    devs[0]=(dev_t){.ip=0xC0A80105,.in_use=1};                 // .5 unnamed unknown
    devs[1]=(dev_t){.ip=0xC0A80106,.in_use=1,.cls=CLS_INFRA};  // .6 unnamed but classed
    strcpy(devs[1].hostname,"");
    devs[2]=(dev_t){.ip=0xC0A80107,.in_use=1};                 // .7
    strcpy(devs[2].hostname,"MyRouter");                       // already named

    // mDNS says .5 is a Chromecast
    apply(0xC0A80105,"Living Room TV",CLS_IOT);
    assert(strcmp(devs[0].hostname,"Living Room TV")==0);
    assert(devs[0].cls==CLS_IOT);
    printf("T1 unnamed device gets name+class: ok (%s / cls=%d)\n",
           devs[0].hostname, devs[0].cls);

    // mDNS says .6 is also a cast device — name fills, but class must NOT
    // overwrite the existing INFRA classification
    apply(0xC0A80106,"Office Speaker",CLS_IOT);
    assert(strcmp(devs[1].hostname,"Office Speaker")==0);
    assert(devs[1].cls==CLS_INFRA);   // preserved!
    printf("T2 existing class preserved: ok (%s / cls=%d stays INFRA)\n",
           devs[1].hostname, devs[1].cls);

    // mDNS tries to rename .7 which already has a name — must NOT clobber
    apply(0xC0A80107,"SomeOtherName",CLS_IOT);
    assert(strcmp(devs[2].hostname,"MyRouter")==0);  // unchanged
    printf("T3 existing name not clobbered: ok (%s)\n", devs[2].hostname);

    // mDNS result for an IP we don't have — no crash, no match
    apply(0xC0A801FE,"Ghost",CLS_IOT);
    printf("T4 unmatched IP ignored: ok\n");

    printf("\nMDNS MATCH LOGIC OK\n");
    return 0;
}
