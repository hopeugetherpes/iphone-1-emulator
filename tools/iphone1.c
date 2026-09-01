#include "board.h"
#include "bringup_os1.h"
#include "bringup.h"
#include "lzss.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *p; size_t n; } blob_t;
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static blob_t read_blob(const char *path) {
    blob_t b={0}; FILE *f=fopen(path,"rb"); long n;
    if (!f) { fprintf(stderr,"cannot open %s: %s\n",path,strerror(errno)); return b; }
    fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
    if (n<=0 || (b.p=malloc((size_t)n))==NULL ||
        fread(b.p,1,(size_t)n,f)!=(size_t)n) { free(b.p); b.p=NULL; }
    fclose(f); b.n=b.p?(size_t)n:0;
    if (!b.p) return b;
    if (b.n>=4 && !memcmp(b.p,"8900",4)) {
        size_t off=0x800, sz=b.n>=16?le32(b.p+12):0;
        if (off+sz>b.n) { free(b.p); return (blob_t){0}; }
        uint8_t *q=malloc(sz); memcpy(q,b.p+off,sz); free(b.p); b=(blob_t){q,sz};
        printf("detected 8900 wrapper, payload=%zu\n",b.n);
    }
    if (b.n>=4 && !memcmp(b.p,"2gmI",4)) {
        size_t off=0x400, sz=b.n>=0x1c?le32(b.p+0x18):0;
        if (off+sz>b.n) { free(b.p); return (blob_t){0}; }
        uint8_t *q=malloc(sz); memcpy(q,b.p+off,sz); free(b.p); b=(blob_t){q,sz};
        printf("detected IMG2 wrapper, payload=%zu\n",b.n);
    }
    lzss_header_t h;
    if (lzss_parse_header(b.p,b.n,&h)) {
        uint8_t *q=malloc(h.uncompressed_size);
        if (!q || lzss_decompress(q,h.uncompressed_size,b.p+LZSS_HEADER_SIZE,
                                  h.compressed_size)!=h.uncompressed_size) {
            free(q); free(b.p); return (blob_t){0};
        }
        free(b.p); b=(blob_t){q,h.uncompressed_size};
        printf("detected complzss, decompressed=%zu\n",b.n);
    } else printf("using raw file, bytes=%zu\n",b.n);
    return b;
}
static int save_ppm(const char *path, const s5l8900_t *m) {
    unsigned k=s5l_clcd_active_window(&m->clcd), w=0,h=0; size_t n;
    uint8_t *rgb;
    if (k==CLCD_WIN_NONE) return 0;
    n=320u*480u*3u; rgb=malloc(n);
    if (!rgb || !s5l_clcd_scanout(&m->clcd,k,m->ram,m->ram_base,m->ram_size,
                                  rgb,n,&w,&h)) { free(rgb); return 0; }
    FILE *f=fopen(path,"wb"); if (!f) { free(rgb); return 0; }
    fprintf(f,"P6\n%u %u\n255\n",w,h); fwrite(rgb,1,(size_t)w*h*3u,f);
    fclose(f); free(rgb); return 1;
}
typedef struct { FILE *serial; const s5l8900_t *m; bool bol; } tap_t;
/* Serial output; each line in the --serial file is prefixed with the guest
 * instruction count so kernel messages can be correlated with --swi-log. */
static void tap(void *ctx,uint8_t c) {
    tap_t *t=ctx; putchar((int)c); fflush(stdout);
    if (t->serial) {
        if (t->bol) fprintf(t->serial,"%llu ",(unsigned long long)t->m->cpu.cycles);
        fputc(c,t->serial); fflush(t->serial); t->bol=(c=='\n');
    }
}
static const char *status_name(arm_status_t s) {
    switch(s) { case ARM_OK:return "ARM_OK"; case ARM_UNDEFINED:return "ARM_UNDEFINED";
    case ARM_HALT:return "ARM_HALT"; case ARM_GUEST_UNDEFINED:return "ARM_GUEST_UNDEFINED"; }
    return "other";
}
static bool g_break_hit;
static bool break_hook(void *ctx) { (void)ctx; g_break_hit=true; return false; }
/* --watch-pc: log every hit of up to 15 PCs without stopping. */
static uint32_t g_watch[S5L_PRE_STEP_TARGET_MAX]; static unsigned g_watch_n;
static uint32_t g_break_pc; static const s5l8900_t *g_watch_m;
static uint32_t g_exec_pc, g_swi_pc; static FILE *g_swi_log;
static int g_break_swi; static bool g_break_swi_set; static uint32_t g_break_swi_ttbr0;
static bool read_va32(s5l8900_t *m, uint32_t va, uint32_t *out) {
    uint32_t pa;
    if (arm_mmu_translate(&m->cpu, va, ARM_ACCESS_READ, true, &pa)!=0 ||
        pa<m->ram_base || pa-m->ram_base+4u>m->ram_size) return false;
    memcpy(out, m->ram+(pa-m->ram_base), 4); return true;
}
static void print_va_str(FILE *o, s5l8900_t *m, uint32_t va) {
    for (unsigned i=0;i<256;i+=4) { uint32_t w; if (!read_va32(m,va+i,&w)) { fputc('?',o); return; }
        for (unsigned b=0;b<4;b++) { char c=(char)(w>>(8*b)); if (!c) return; fputc(c,o); } }
}
static bool watch_hook(void *ctx) {
    (void)ctx; s5l8900_t *m=(s5l8900_t*)g_watch_m; uint32_t pc=m->cpu.r[15]&~1u;
    if (g_break_pc && pc==g_break_pc) { g_break_hit=true; return false; }
    if (g_exec_pc && pc==g_exec_pc) {
        /* XNU execve(proc, uap, retval): uap->fname is a user_addr_t whose
         * low word is at [r1]; the caller's map is still current. */
        uint32_t fname=0; read_va32(m,m->cpu.r[1],&fname);
        printf("EXEC cyc=%llu ",(unsigned long long)m->cpu.cycles); print_va_str(stdout,m,fname); puts("");
        return false;
    }
    if (g_swi_pc && pc==g_swi_pc) {
        /* SWI vector entry (SVC mode): lr = user PC+4, r12 = Darwin syscall
         * number, r0-r3 = args; ttbr0 identifies the address space. */
        fprintf(g_swi_log,"%llu %08x %08x %d %08x %08x %08x %08x\n",(unsigned long long)m->cpu.cycles,
                m->cpu.cp15.ttbr0,m->cpu.r[14]-4u,(int)m->cpu.r[12],m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3]);
        int no=(int)m->cpu.r[12];
        if (g_break_swi_set && no==g_break_swi &&
            (!g_break_swi_ttbr0 || m->cpu.cp15.ttbr0==g_break_swi_ttbr0)) { g_break_hit=true; return false; }
        if (no==-31) { /* mach_msg_trap: header {bits,size,remote,local,reserved,id} */
            uint32_t hb=0,rp=0,lp=0,id=0; read_va32(m,m->cpu.r[0],&hb); read_va32(m,m->cpu.r[0]+8u,&rp);
            read_va32(m,m->cpu.r[0]+12u,&lp); read_va32(m,m->cpu.r[0]+20u,&id);
            fprintf(g_swi_log,"  msg bits=%08x remote=%x local=%x id=%d\n",hb,rp,lp,(int)id);
        } else if (no==5||no==188||no==33||no==59) { /* open, stat, access, execve */
            fputs("  path ",g_swi_log); print_va_str(g_swi_log,m,m->cpu.r[0]); fputc('\n',g_swi_log);
        }
        return false;
    }
    printf("W %08x cyc=%llu tb=%llu r0=%08x r1=%08x r2=%08x r3=%08x r5=%08x sp=%08x lr=%08x\n",pc,
           (unsigned long long)m->cpu.cycles,(unsigned long long)m->timer.ticks,
           m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[3],m->cpu.r[5],m->cpu.r[13],m->cpu.r[14]);
    return false;
}

#define TRACE_RING (1u<<21)
typedef struct { uint32_t pc, cpsr, r0, r1, r2, r3, lr, sp; } trace_ent_t;
static trace_ent_t g_trace[TRACE_RING]; static unsigned g_trace_n;
static void trace_record(const s5l8900_t *m) {
    trace_ent_t *e=&g_trace[g_trace_n++%TRACE_RING];
    e->pc=m->cpu.r[15]; e->cpsr=m->cpu.cpsr; e->r0=m->cpu.r[0]; e->r1=m->cpu.r[1];
    e->r2=m->cpu.r[2]; e->r3=m->cpu.r[3]; e->lr=m->cpu.r[14]; e->sp=m->cpu.r[13];
}
static void trace_dump(unsigned last) {
    unsigned n=g_trace_n<TRACE_RING?g_trace_n:TRACE_RING; if (last<n) n=last;
    unsigned start=g_trace_n-n;
    for (unsigned i=0;i<n;i++) { const trace_ent_t *e=&g_trace[(start+i)%TRACE_RING];
        printf("T %08x %c%02x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x lr=%08x\n",e->pc,
               (e->cpsr&0x20)?'t':'a',e->cpsr&0x1f,e->r0,e->r1,e->r2,e->r3,e->sp,e->lr); }
}
/* --dump-va: hex-dump guest memory (translated through the current page
 * tables) when the run ends. */
static void dump_va(s5l8900_t *m, uint32_t va, unsigned words) {
    printf("dump %08x:", va);
    for (unsigned i=0;i<words;i++) {
        uint32_t pa, v=0;
        if (arm_mmu_translate(&m->cpu, va+4u*i, ARM_ACCESS_READ, true, &pa)==0 &&
            pa>=m->ram_base && pa-m->ram_base+4u<=m->ram_size)
            memcpy(&v, m->ram+(pa-m->ram_base), 4);
        else { printf(" ????????"); continue; }
        printf("%s %08x", (i%8==0&&i)?"\n                ":"", v);
    }
    puts("");
}
static void usage(void) {
    puts("iphone1 --board m68|n82 --kernel PATH --devicetree PATH [options]");
}
int main(int argc,char **argv) {
    const char *board_name=NULL,*kp=NULL,*dp=NULL,*rp=NULL,*cmd=NULL,*serial=NULL,*shot=NULL;
    uint64_t steps=200000000ull; unsigned ram_mb=0,vd=0; bool fb=true,debug=false,trace=false;
    uint32_t break_pc=0; uint64_t trace_from=UINT64_MAX; unsigned trace_last=256;
    uint32_t dump[8]; unsigned dump_n=0;
    const char *unmatch[16]; unsigned unmatch_n=0; uint64_t shot_every=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--board")&&i+1<argc) board_name=argv[++i];
        else if (!strcmp(argv[i],"--kernel")&&i+1<argc) kp=argv[++i];
        else if (!strcmp(argv[i],"--devicetree")&&i+1<argc) dp=argv[++i];
        else if (!strcmp(argv[i],"--ramdisk")&&i+1<argc) rp=argv[++i];
        else if (!strcmp(argv[i],"--ram-mb")&&i+1<argc) ram_mb=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--cmdline")&&i+1<argc) cmd=argv[++i];
        else if (!strcmp(argv[i],"--steps")&&i+1<argc) steps=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--no-fb")) fb=false;
        else if (!strcmp(argv[i],"--v-display")&&i+1<argc) vd=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--debug-enabled")) debug=true;
        else if (!strcmp(argv[i],"--serial")&&i+1<argc) serial=argv[++i];
        else if (!strcmp(argv[i],"--screenshot")&&i+1<argc) shot=argv[++i];
        else if (!strcmp(argv[i],"--shot-every")&&i+1<argc) shot_every=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--trace-devices")) trace=true;
        else if (!strcmp(argv[i],"--verbose")) { }
        else if (!strcmp(argv[i],"--break-pc")&&i+1<argc) break_pc=(uint32_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--watch-pc")&&i+1<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u)
            g_watch[g_watch_n++]=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
        else if (!strcmp(argv[i],"--trace-from")&&i+1<argc) trace_from=strtoull(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--trace-last")&&i+1<argc) trace_last=(unsigned)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"--unmatch")&&i+1<argc&&unmatch_n<16) unmatch[unmatch_n++]=argv[++i];
        else if (!strcmp(argv[i],"--swi-log")&&i+2<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u) {
            g_watch[g_watch_n++]=g_swi_pc=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
            g_swi_log=fopen(argv[++i],"w"); if (!g_swi_log) { perror("--swi-log"); return 1; } }
        else if (!strcmp(argv[i],"--exec-pc")&&i+1<argc&&g_watch_n<S5L_PRE_STEP_TARGET_MAX-1u)
            g_watch[g_watch_n++]=g_exec_pc=(uint32_t)strtoul(argv[++i],NULL,0)&~1u;
        else if (!strcmp(argv[i],"--break-swi")&&i+2<argc) { g_break_swi_set=true;
            g_break_swi=(int)strtol(argv[++i],NULL,0); g_break_swi_ttbr0=(uint32_t)strtoul(argv[++i],NULL,0); }
        else if (!strcmp(argv[i],"--dump-va")&&i+1<argc&&dump_n<8) dump[dump_n++]=(uint32_t)strtoul(argv[++i],NULL,0);
        else { usage(); return 2; }
    }
    const s5l_board_t *b=s5l_board_find(board_name);
    if (!b||!kp||!dp) { usage(); return 2; }
    blob_t k=read_blob(kp), d=read_blob(dp), rd=rp?read_blob(rp):(blob_t){0};
    if (!k.p||!d.p||(rp&&!rd.p)) { free(k.p);free(d.p);free(rd.p);return 1; }
    char why[256];
    if (!s5l_board_check_devicetree(b,d.p,d.n,why,sizeof why)) {
        fprintf(stderr,"device tree rejected: %s\n",why); return 1;
    }
    s5l8900_t m;
    if (!s5l_board_init(b,&m,ram_mb?ram_mb<<20:0)) {
        fprintf(stderr,"machine init failed (RAM may overlap a device window)\n");
        return 1;
    }
    m.trace_devices=trace;
    s5l_os1_request_t q={k.p,k.n,d.p,d.n,rd.p,rd.n,cmd,fb,vd,b->lcd_width,b->lcd_height,debug,
                         unmatch,unmatch_n};
    s5l_os1_result_t r;
    if (!s5l_os1_boot(&m,&q,&r)) { fprintf(stderr,"boot failed: %s\n",r.detail);
        s5l8900_free(&m); return 1; }
    printf("layout virt_base=0x%08x phys_base=0x%08x ram_size=0x%08x\n",
           r.virt_base,r.phys_base,r.ram_size);
    printf("kernel_begin_pa=0x%08x kernel_end_pa=0x%08x entry_pa=0x%08x entry_va=0x%08x\n",
           r.kernel_begin_pa,r.kernel_end_pa,r.entry_pa,r.entry_va);
    printf("devicetree_pa=0x%08x devicetree_va=0x%08x devicetree_size=0x%08x\n",
           r.devicetree_pa,r.devicetree_va,r.devicetree_size);
    printf("boot_args_pa=0x%08x ramdisk_pa=0x%08x ramdisk_va=0x%08x ramdisk_size=0x%08x\n",
           r.boot_args_pa,r.ramdisk_pa,r.ramdisk_va,r.ramdisk_size);
    printf("framebuffer_pa=0x%08x vram_bytes=0x%08x top_of_kernel_data_pa=0x%08x cmdline=\"%s\"\n",
           r.framebuffer_pa,r.vram_bytes,r.top_of_kernel_data_pa,r.cmdline);
    tap_t t={serial?fopen(serial,"wb"):NULL,&m,true}; s5l8900_set_uart0_host(&m,tap,&t);
    arm_status_t st=ARM_OK; uint64_t total=0; uint32_t ring[32]={0}; unsigned ri=0, same=0;
    uint32_t last_window=0; const char *reason="steps exhausted";
    if (g_watch_n) { g_watch_m=&m; g_break_pc=break_pc&~1u;
        if (g_break_pc) g_watch[g_watch_n++]=g_break_pc;
        if (!s5l8900_set_pre_step_hook(&m,watch_hook,NULL,g_watch,g_watch_n)) fprintf(stderr,"watch hook refused\n"); }
    else if (break_pc) { uint32_t tgt=break_pc&~1u;
        if (!s5l8900_set_pre_step_hook(&m,break_hook,NULL,&tgt,1)) fprintf(stderr,"break hook refused\n"); }
    while (total<steps) {
        unsigned slice=(unsigned)((steps-total)>1000000ull?1000000ull:steps-total);
        /* trace_from is in retired instructions (cpu.cycles), the unit the
         * EXEC/SWI logs report. */
        if (m.cpu.cycles>=trace_from) { slice=1; trace_record(&m); }
        else if (m.cpu.cycles+slice>trace_from) slice=(unsigned)(trace_from-m.cpu.cycles);
        unsigned n=s5l8900_run(&m,slice,&st); total+=n; ring[ri++%32]=m.cpu.r[15];
        if (shot&&shot_every&&total/shot_every!=(total-n)/shot_every) {
            char p[512]; snprintf(p,sizeof p,"%s.%llu.ppm",shot,(unsigned long long)(total/shot_every));
            if (!save_ppm(p,&m)) printf("periodic screenshot %s: no active CLCD window\n",p);
        }
        if (g_break_hit) { reason="break-pc"; break; }
        uint32_t win=m.cpu.r[15]&~63u;
        if (win==last_window) same++; else same=0; last_window=win;
        if (st!=ARM_OK) { reason=status_name(st); break; }
        if (same>=20 && slice>1) { reason="spin"; break; }
        if (!n) break;
    }
    printf("\nreason=%s instructions=%llu pc=0x%08x cpsr=0x%08x\n",reason,
           (unsigned long long)total,m.cpu.r[15],m.cpu.cpsr);
    for (unsigned i=0;i<16;i++) printf("r%u=0x%08x%s",i,m.cpu.r[i],i%4==3?"\n":" ");
    printf("ttbr0=0x%08x sctlr=0x%08x M=%u unmapped_reads=%llu unmapped_writes=%llu\n",
           m.cpu.cp15.ttbr0,m.cpu.cp15.sctlr,(m.cpu.cp15.sctlr&1u)!=0,
           (unsigned long long)m.unmapped_reads,(unsigned long long)m.unmapped_writes);
    if (m.unmapped_addr_count) {
        printf("unmapped-pages:");
        for (unsigned i=0;i<m.unmapped_addr_count;i++) printf(" 0x%08x",m.unmapped_addr[i]);
        puts("");
    }
    printf("last-pcs:");
    unsigned ring_n = ri < 32u ? ri : 32u;
    unsigned ring_start = ri < 32u ? 0u : ri % 32u;
    for (unsigned i=0;i<ring_n;i++)
        printf(" 0x%08x",ring[(ring_start+i)%32u]);
    puts("");
    if (g_trace_n) trace_dump(trace_last);
    for (unsigned i=0;i<dump_n;i++) dump_va(&m,dump[i],64);
    if (trace) {
        printf("mmio-census (%u):", m.dev_count);
        for (unsigned i=0;i<m.dev_count;i++)
            printf(" %c0x%08x=0x%08x",m.dev_is_write[i]?'W':'R',
                   m.dev_addr[i],m.dev_value[i]);
        puts("");
    }
    if (st!=ARM_OK) {
        uint32_t pc=m.cpu.r[15], off=pc-m.ram_base;
        if (pc>=m.ram_base&&off+4<=m.ram_size)
            printf("bytes-at-pc=%02x%02x%02x%02x\n",m.ram[off],m.ram[off+1],m.ram[off+2],m.ram[off+3]);
    }
    if (shot&&!save_ppm(shot,&m)) fprintf(stderr,"screenshot failed: %s\n",shot);
    if (t.serial) fclose(t.serial); s5l8900_free(&m); free(k.p);free(d.p);free(rd.p); return 0;
}
