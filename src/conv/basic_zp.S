; BASIC's CHRGET lives at $0073 and TXTPTR at $007A. The compiler uses
; $02-$8F. Copy them back before unmap-basic maps the ROM in again.
; Straight-line: .init and .fini fall through. Do not RTS.

.section .noinit,"aw",@nobits
basic_zp:
  .space 0x8e

.section .init.005,"ax",@progbits
  ldx #0
save_zp:
  lda $02,x
  sta basic_zp,x
  inx
  cpx #$8e
  bne save_zp

.section .fini.989,"ax",@progbits
  ldx #0
load_zp:
  lda basic_zp,x
  sta $02,x
  inx
  cpx #$8e
  bne load_zp
