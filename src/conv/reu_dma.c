#include "w64reu.h"

#define REU_STATUS   (*(volatile unsigned char *)0xDF00)
#define REU_COMMAND  (*(volatile unsigned char *)0xDF01)
#define REU_C64_LO   (*(volatile unsigned char *)0xDF02)
#define REU_C64_HI   (*(volatile unsigned char *)0xDF03)
#define REU_ADDR_LO  (*(volatile unsigned char *)0xDF04)
#define REU_ADDR_HI  (*(volatile unsigned char *)0xDF05)
#define REU_BANK     (*(volatile unsigned char *)0xDF06)
#define REU_LEN_LO   (*(volatile unsigned char *)0xDF07)
#define REU_LEN_HI   (*(volatile unsigned char *)0xDF08)
#define REU_CONTROL  (*(volatile unsigned char *)0xDF0A)
#define REU_STASH    0x90
#define REU_FETCH    0x91

static unsigned long reu_bytes;
static unsigned char reu_on;

#define DMA_BARRIER() __asm__ volatile("" ::: "memory")

static unsigned char reu_detect(void) {
    volatile unsigned char orig;
    volatile unsigned char test;

    orig = REU_ADDR_LO;
    REU_ADDR_LO = 0xAA;
    test = REU_ADDR_LO;
    if (test != 0xAA) {
        REU_ADDR_LO = orig;
        return 0;
    }
    REU_ADDR_LO = 0x55;
    test = REU_ADDR_LO;
    REU_ADDR_LO = orig;
    return test == 0x55;
}

static void dma(unsigned char cmd, unsigned long addr, void *ram, unsigned int n) {
    unsigned char *p = (unsigned char *)ram;

    while (n) {
        unsigned int chunk = n > 256 ? 256 : n;

        REU_C64_LO = (unsigned char)((unsigned int)p & 0xFF);
        REU_C64_HI = (unsigned char)(((unsigned int)p >> 8) & 0xFF);
        REU_ADDR_LO = (unsigned char)(addr & 0xFF);
        REU_ADDR_HI = (unsigned char)((addr >> 8) & 0xFF);
        REU_BANK = (unsigned char)((addr >> 16) & 0xFF);
        REU_LEN_LO = (unsigned char)(chunk & 0xFF);
        REU_LEN_HI = (unsigned char)((chunk >> 8) & 0xFF);
        REU_CONTROL = 0;
        if (cmd == REU_STASH)
            DMA_BARRIER();
        REU_COMMAND = cmd;
        DMA_BARRIER();
        addr += chunk;
        p += chunk;
        n -= chunk;
    }
}

void w64_store_get(unsigned long addr, void *dst, unsigned int n) {
    if (n)
        dma(REU_FETCH, addr, dst, n);
}

void w64_store_put(unsigned long addr, const void *src, unsigned int n) {
    if (n)
        dma(REU_STASH, addr, (void *)src, n);
}

static unsigned long measure(void) {
    static volatile unsigned char test_byte;
    static volatile unsigned char read_byte;
    unsigned long size = 0;
    unsigned int bank;

    for (bank = 0; bank < 255; bank++) {
        test_byte = 0xA5;
        REU_C64_LO = (unsigned char)((unsigned int)&test_byte & 0xFF);
        REU_C64_HI = (unsigned char)(((unsigned int)&test_byte >> 8) & 0xFF);
        REU_ADDR_LO = 0;
        REU_ADDR_HI = 0;
        REU_BANK = (unsigned char)bank;
        REU_LEN_LO = 1;
        REU_LEN_HI = 0;
        REU_CONTROL = 0;
        DMA_BARRIER();
        REU_COMMAND = REU_STASH;
        DMA_BARRIER();
        test_byte = 0x5A;
        read_byte = 0;
        REU_C64_LO = (unsigned char)((unsigned int)&read_byte & 0xFF);
        REU_C64_HI = (unsigned char)(((unsigned int)&read_byte >> 8) & 0xFF);
        REU_ADDR_LO = 0;
        REU_ADDR_HI = 0;
        REU_BANK = (unsigned char)bank;
        REU_LEN_LO = 1;
        REU_LEN_HI = 0;
        REU_CONTROL = 0;
        REU_COMMAND = REU_FETCH;
        DMA_BARRIER();
        if (read_byte != 0xA5)
            break;
        size = ((unsigned long)(bank + 1)) << 16;
    }
    return size;
}

void w64_reu_probe(void) {
    reu_on = reu_detect();
    reu_bytes = 0;
    if (!reu_on)
        return;
    REU_CONTROL = 0;
    REU_BANK = 0;
    reu_bytes = measure();
}

unsigned long w64_reu_bytes(void) {
    return reu_bytes;
}
