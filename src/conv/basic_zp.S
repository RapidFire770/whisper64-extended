; BASIC's CHRGET lives at $0073 and TXTPTR at $007A. The compiler uses
; $02-$8F. Copy them back before unmap-basic maps the ROM in again.
; Straight-line: .init and .fini fall through. Do not RTS.
;
; Do not exit by returning from main. __exit_return stores __rc16 ($12)
; and __rc20 ($16) after .fini.989. $16 is TEMPPT. A filename is a string,
; so LOAD breaks and PRINT 1 does not. tool_exit_ready copies the bytes
; and jumps through the warm-start vector at $A002.
;
; JiffyDOS CLALL ($FFE7) zeros the file count and does not send CLOSE.
; A data channel left open on the 1541 makes the next LOAD time out.
; BASIC prints FILE NOT FOUND and the program at $0801 stays put.
; Close every logical file first, then send the DOS initialize ("I")
; so the drive drops a channel this table no longer names.
; $FFC3 returns with X = 0, so the 16-close cap is drain_left.

.section .noinit,"aw",@nobits
basic_zp:
  .space 0x8e

.section .bss,"aw",@nobits
drain_left:
  .space 1

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

.section .text.tool_exit_ready,"ax",@progbits
.globl tool_exit_ready
tool_exit_ready:
  pha
  jsr $ff8a
  lda #16
  sta drain_left
exit_close:
  lda $98
  beq exit_closed
  lda $0259
  jsr $ffc3
  dec drain_left
  bne exit_close
exit_closed:
  jsr $ffab
  jsr $ffae
  jsr $ffe7
  jsr $ff81
  lda #14
  sta $d020
  lda #6
  sta $d021
  lda #$15
  sta $d018
  lda #14
  sta $0286
  jsr exit_release
  pla
  pha
  tax
  lda #15
  ldy #15
  jsr $ffba
  lda #'I'
  sta $033c
  lda #1
  ldx #$3c
  ldy #$03
  jsr $ffbd
  lda #0
  sta $90
  jsr $ffc0
  bcs exit_close15
  ldx #15
  jsr $ffc6
  bcs exit_close15
  lda #40
  sta drain_left
exit_drain:
  lda #0
  sta $90
  jsr $ffcf
  cmp #$0d
  beq exit_close15
  jsr $ffb7
  bne exit_close15
  dec drain_left
  bne exit_drain
exit_close15:
  jsr $ffcc
  lda #15
  jsr $ffc3
exit_nodrain:
  lda #0
  tax
  tay
  jsr $ffbd
  jsr exit_release
  pla
  sta $ba
  ldx #0
exit_zp:
  lda basic_zp,x
  sta $02,x
  inx
  cpx #$8e
  bne exit_zp
  lda #$37
  sta $01
  cli
  jmp ($a002)

exit_release:
  jsr $ffab
  jsr $ffae
  lda $dd00
  ora #$10
  sta $dd00
  lda #0
  sta $90
  rts
