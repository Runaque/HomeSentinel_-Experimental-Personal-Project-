#include <stdio.h>
#include <string.h>
#include <assert.h>

// verbatim from notifier.c
static void json_escape(const char *in, char *out, size_t out_len){
    size_t o=0;
    for(size_t i=0; in[i]!='\0' && o+2<out_len; i++){
        char c=in[i];
        if(c=='"'||c=='\\'){ out[o++]='\\'; out[o++]=c; }
        else if(c=='\n'){ if(o+2<out_len){ out[o++]='\\'; out[o++]='n'; } }
        else if((unsigned char)c>=0x20){ out[o++]=c; }
    }
    out[o]='\0';
}

int main(void){
    char out[200];

    json_escape("Living Room TV", out, sizeof(out));
    assert(strcmp(out,"Living Room TV")==0);
    printf("plain: %s\n", out);

    json_escape("Bob's \"NAS\"", out, sizeof(out));
    assert(strcmp(out,"Bob's \\\"NAS\\\"")==0);
    printf("quotes: %s\n", out);

    json_escape("path\\to\\thing", out, sizeof(out));
    assert(strcmp(out,"path\\\\to\\\\thing")==0);
    printf("backslash: %s\n", out);

    json_escape("line1\nline2", out, sizeof(out));
    assert(strcmp(out,"line1\\nline2")==0);
    printf("newline: %s\n", out);

    // control char (bell 0x07) should be stripped
    json_escape("a\x07""b", out, sizeof(out));
    assert(strcmp(out,"ab")==0);
    printf("control stripped: %s\n", out);

    // truncation safety: tiny buffer, must not overflow, must NUL-terminate
    char tiny[8];
    json_escape("\"\"\"\"\"\"\"\"\"\"", tiny, sizeof(tiny));
    assert(strlen(tiny) < sizeof(tiny));
    printf("truncated safe: '%s' (len %zu)\n", tiny, strlen(tiny));

    printf("\nJSON ESCAPE OK\n");
    return 0;
}
