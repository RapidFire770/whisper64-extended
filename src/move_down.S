#include "imag.inc"

; move_bytes calls this when dst > src. mos-c64-clang passes dst in
; __rc2, src in __rc4, and n in A/X. __rc6 is caller-saved scratch.
; __rc0/__rc1 are the software stack pointer and stay put.
; Every caller shifts by one 80-byte row, so a row copied forward does
; not overlap. Rows are taken from the end of the block back to the start.
; A leftover n % 80 is copied first, at that high end.

.section .text.move_bytes_down,"ax",@progbits
.globl move_bytes_down
move_bytes_down:
  sta __rc6
  stx __rc7
  clc
  lda __rc4
  adc __rc6
  sta __rc4
  lda __rc5
  adc __rc7
  sta __rc5
  clc
  lda __rc2
  adc __rc6
  sta __rc2
  lda __rc3
  adc __rc7
  sta __rc3
  ldx #0
  ldy #0
div80:
  lda __rc6
  cmp #80
  lda __rc7
  sbc #0
  bcc div_done
  lda __rc6
  sec
  sbc #80
  sta __rc6
  lda __rc7
  sbc #0
  sta __rc7
  inx
  bne div80
  iny
  jmp div80
div_done:
  lda __rc6
  beq stash_rows
  lda __rc4
  sec
  sbc __rc6
  sta __rc4
  lda __rc5
  sbc #0
  sta __rc5
  lda __rc2
  sec
  sbc __rc6
  sta __rc2
  lda __rc3
  sbc #0
  sta __rc3
stash_rows:
  lda __rc6
  stx __rc6
  sty __rc7
  tax
  beq row_loop
  ldy #0
rem_copy:
  lda (__rc4),y
  sta (__rc2),y
  iny
  dex
  bne rem_copy
row_loop:
  lda __rc6
  ora __rc7
  beq done
  lda __rc4
  sec
  sbc #80
  sta __rc4
  lda __rc5
  sbc #0
  sta __rc5
  lda __rc2
  sec
  sbc #80
  sta __rc2
  lda __rc3
  sbc #0
  sta __rc3
  ldy #0
row_copy:
  lda (__rc4),y
  sta (__rc2),y
  iny
  cpy #80
  bne row_copy
  lda __rc6
  bne dec_lo
  dec __rc7
dec_lo:
  dec __rc6
  jmp row_loop
done:
  rts
