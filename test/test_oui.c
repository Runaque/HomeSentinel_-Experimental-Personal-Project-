// Compile the REAL binary-search logic from oui.c against the blob the
// Python tool generated, proving format + search agree.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdlib.h>

#define OUI_VENDOR_LEN 28
#define OUI_RECORD_SIZE 32

typedef struct __attribute__((packed)) {
    uint8_t oui24[3];
    uint8_t reserved;
    char vendor[OUI_VENDOR_LEN];
} oui_record_t;

_Static_assert(sizeof(oui_record_t)==OUI_RECORD_SIZE,"32 bytes");

static const uint8_t *g_records;
static uint32_t g_count;

// strlcpy shim for glibc
static size_t strlcpy_(char *d, const char *s, size_t n){
    size_t l=strlen(s); if(n){ size_t c=(l>=n)?n-1:l; memcpy(d,s,c); d[c]=0; } return l;
}

// --- verbatim logic from oui_lookup ---
static int oui_cmp(const uint8_t key[3], const uint8_t rec[3]){ return memcmp(key,rec,3); }

static int lookup(const uint8_t mac[6], char *out, size_t out_len){
    if(mac[0]&0x02){ strlcpy_(out,"Randomized (private)",out_len); return 0; }
    if(g_count==0){ strlcpy_(out,"Unknown",out_len); return 0; }
    const uint8_t key[3]={mac[0],mac[1],mac[2]};
    int32_t lo=0, hi=(int32_t)g_count-1;
    while(lo<=hi){
        int32_t mid=lo+(hi-lo)/2;
        const oui_record_t *rec=(const oui_record_t*)(g_records+(size_t)mid*OUI_RECORD_SIZE);
        int c=oui_cmp(key,rec->oui24);
        if(c==0){ strlcpy_(out,rec->vendor,out_len); out[out_len-1]=0; return 1; }
        else if(c<0) hi=mid-1; else lo=mid+1;
    }
    strlcpy_(out,"Unknown",out_len); return 0;
}

int main(void){
    FILE *f=fopen(getenv("OUI_BIN")?getenv("OUI_BIN"):"sample.bin","rb");
    assert(f);
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *blob=malloc(sz); fread(blob,1,sz,f); fclose(f);

    assert(memcmp(blob,"OUI1",4)==0);
    memcpy(&g_count,blob+4,4);
    g_records=blob+8;
    printf("loaded %u records\n", g_count);

    char out[OUI_VENDOR_LEN];
    struct { uint8_t mac[6]; const char *expect; int found; } cases[] = {
        {{0x3C,0x22,0xFB,0x11,0x22,0x33}, "Apple Inc.", 1},
        {{0xF0,0x99,0x1C,0x00,0x00,0x01}, "Espressif Inc.", 1},
        {{0xB8,0x27,0xEB,0xAA,0xBB,0xCC}, "Raspberry Pi Foundation", 1},
        {{0xA4,0xE5,0x7C,0x00,0x00,0x00}, "Dell Inc.", 1},
        {{0x00,0x00,0x00,0x00,0x00,0x00}, "XEROX CORPORATION", 1},  // first
        {{0xF0,0x99,0x1C,0xFF,0xFF,0xFF}, "Espressif Inc.", 1},     // last-ish
        {{0x10,0x34,0x56,0x00,0x00,0x00}, "Unknown", 0},           // absent
        {{0x6A,0x4D,0x2B,0x00,0x00,0x00}, "Randomized (private)", 0}, // LAA bit
    };
    int npass=0, n=sizeof(cases)/sizeof(cases[0]);
    for(int i=0;i<n;i++){
        int found=lookup(cases[i].mac,out,sizeof(out));
        int ok=(found==cases[i].found)&&(strcmp(out,cases[i].expect)==0);
        printf("  %02X:%02X:%02X -> '%s' (found=%d) %s\n",
               cases[i].mac[0],cases[i].mac[1],cases[i].mac[2],out,found,
               ok?"OK":"FAIL");
        if(ok) npass++;
    }
    printf("\n%d/%d passed\n", npass, n);
    free(blob);
    return (npass==n)?0:1;
}
