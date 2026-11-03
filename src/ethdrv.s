; Contiki ethernet wrapper; bufsize matches our eth_inp (860), not stock 1518: the driver drops longer frames.

.include "common.inc"

.export eth_init
.export eth_rx
.export eth_tx

.import eth_inp
.import eth_inp_len
.import eth_outp
.import eth_outp_len

.import eth

.import cfg_mac

ETH_BUF = 880

.struct driver
  drvtype .byte 3
  apiver  .byte
  mac     .byte 6
  bufaddr .addr
  bufsize .word
  init    .byte 3
  poll    .byte 3
  send    .byte 3
  exit    .byte 3
.endstruct

.code

eth_init:
  jsr eth+driver::init
  ldx #5
: lda eth+driver::mac,x
  sta cfg_mac,x
  dex
  bpl :-
  ldax #ETH_BUF
  stax eth+driver::bufsize
  rts

eth_rx:
  ldax #eth_inp
  stax eth+driver::bufaddr
  jsr eth+driver::poll
  stax eth_inp_len
  rts

eth_tx:
  ldax #eth_outp
  stax eth+driver::bufaddr
  ldax eth_outp_len
  jmp eth+driver::send
