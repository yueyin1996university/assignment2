#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet) {
  int checksum = 0;
  int i;
  checksum = packet.seqnum + packet.acknum;
  for (i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);
  return checksum;
}

bool IsCorrupted(struct pkt packet) {
  return packet.checksum != ComputeChecksum(packet);
}

/********* Sender (A) *********/
static struct pkt buffer[SEQSPACE];
static bool acked[SEQSPACE];
static bool timer_active[SEQSPACE];
static int base, nextseqnum;

void A_output(struct msg message) {
  int i;
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE < WINDOWSIZE) {
    struct pkt p;
    p.seqnum = nextseqnum;
    p.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) p.payload[i] = message.data[i];
    p.checksum = ComputeChecksum(p);
    buffer[nextseqnum] = p;
    acked[nextseqnum] = false;
    tolayer3(A, p);
    starttimer(A, RTT);
    timer_active[nextseqnum] = true;
    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  } else {
    window_full++;
  }
}

void A_input(struct pkt packet) {
  int seq;
  if (!IsCorrupted(packet)) {
    seq = packet.acknum;
    if (!acked[seq]) {
      acked[seq] = true;
      new_ACKs++;
      stoptimer(A);
      timer_active[seq] = false;
      while (acked[base]) {
        acked[base] = false;
        base = (base + 1) % SEQSPACE;
      }
    }
    total_ACKs_received++;
  }
}

void A_timerinterrupt(void) {
  int i;
  for (i = 0; i < SEQSPACE; i++) {
    if (!acked[i] && timer_active[i]) {
      tolayer3(A, buffer[i]);
      packets_resent++;
      starttimer(A, RTT);
    }
  }
}

void A_init(void) {
  int i;
  base = 0;
  nextseqnum = 0;
  for (i = 0; i < SEQSPACE; i++) {
    acked[i] = false;
    timer_active[i] = false;
  }
}

/********* Receiver (B) *********/
static struct pkt recv_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expected = 0;

void B_input(struct pkt packet) {
  struct pkt ackpkt;
  int seq, i;
  if (!IsCorrupted(packet)) {
    seq = packet.seqnum;
    if (!received[seq]) {
      received[seq] = true;
      recv_buffer[seq] = packet;
    }
    ackpkt.seqnum = 0;
    ackpkt.acknum = seq;
    for (i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
    while (received[expected]) {
      tolayer5(B, recv_buffer[expected].payload);
      received[expected] = false;
      expected = (expected + 1) % SEQSPACE;
      packets_received++;
    }
  }
}

void B_init(void) {
  int i;
  expected = 0;
  for (i = 0; i < SEQSPACE; i++) received[i] = false;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
