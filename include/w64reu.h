#ifndef W64REU_H
#define W64REU_H

void w64_reu_probe(void);
unsigned long w64_reu_bytes(void);
void w64_store_get(unsigned long addr, void *dst, unsigned int n);
void w64_store_put(unsigned long addr, const void *src, unsigned int n);

#endif
