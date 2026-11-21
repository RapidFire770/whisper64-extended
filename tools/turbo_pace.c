#include <peekpoke.h>
#include <stdint.h>

#ifndef PACE_CMD
#define PACE_CMD 0
#endif

static unsigned int pace(void) {
    unsigned int n = 0;
    unsigned char st;

    POKE(0xDC0F, 0);
    POKE(0xDC06, 0x20);
    POKE(0xDC07, 0x4E);
    POKE(0xDC0F, 0x19);
    do {
        n++;
        st = PEEK(0xDC0D);
    } while ((st & 0x02) == 0);
    return n;
}

int main(void) {
    unsigned int n;
    unsigned char b;

    __asm__("sei");
#if PACE_CMD == 1
    b = PEEK(0xD031);
    POKE(0xD031, b & 0x80);
#elif PACE_CMD == 2
    b = PEEK(0xD030);
    POKE(0xD030, b & 0xFE);
#else
    b = PEEK(0xD031);
#endif
    n = pace();
    POKE(0xC000, (unsigned char)(n & 0xFF));
    POKE(0xC001, (unsigned char)(n >> 8));
    POKE(0xC002, b);
    POKE(0xC003, PEEK(0xD030));
    POKE(0xC004, PEEK(0xD031));
    for (;;)
        ;
}
