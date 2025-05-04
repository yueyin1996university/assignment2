#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "gbn.h"

#define RTT 16.0
#define WINDOWSIZE 4
#define SEQSPACE 8
#define NOTINUSE (-1)

static struct pkt buffer[WINDOWSIZE];
static bool acked[SEQSPACE];
static int windowfirst, windowlast;
static int windowcount;
static int A_nextseqnum;

int ComputeChecksum(struct pkt packet) {
  int checksum = 0;
  int i;
  checksum = packet.seqnum + packet.acknum;
  for (i = 0; i < 20; i++) {
    checksum += (int)(packet.payload[i]);
  }
  return checksum;
}

bool IsCorrupted(struct pkt packet) {
  return packet.checksum != ComputeChecksum(packet);
}

void A_output(struct msg message) {
  struct pkt sendpkt;
  int i;

  if (windowcount < WINDOWSIZE) {
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[sendpkt.seqnum] = false;
    windowcount++;

    tolayer3(A, sendpkt);
    if (windowcount == 1)
      starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  } else {
    window_full++;
  }
}

void A_input(struct pkt packet) {
  int acknum = packet.acknum;

  if (!IsCorrupted(packet)) {
    total_ACKs_received++;

    if (!acked[acknum]) {
      acked[acknum] = true;
      new_ACKs++;

      while (windowcount > 0 && acked[buffer[windowfirst].seqnum]) {
        acked[buffer[windowfirst].seqnum] = false;
        windowfirst = (windowfirst + 1) % WINDOWSIZE;
        windowcount--;
      }

      stoptimer(A);
      if (windowcount > 0)
        starttimer(A, RTT);
    }
  }
}

void A_timerinterrupt(void) {
  int i, index;
  for (i = 0; i < windowcount; i++) {
    index = (windowfirst + i) % WINDOWSIZE;
    tolayer3(A, buffer[index]);
    packets_resent++;
    if (i == 0) starttimer(A, RTT);
  }
}

void A_init(void) {
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < SEQSPACE; i++) {
    acked[i] = false;
  }
}

/********* Receiver (B) ************/

static bool received[SEQSPACE];
static char recvbuffer[SEQSPACE][20];
static int expectedseqnum;

void B_input(struct pkt packet) {
  struct pkt ackpkt;
  int i;

  if (!IsCorrupted(packet)) {
    if (!received[packet.seqnum]) {
      for (i = 0; i < 20; i++)
        recvbuffer[packet.seqnum][i] = packet.payload[i];
      received[packet.seqnum] = true;
      packets_received++;

      while (received[expectedseqnum]) {
        tolayer5(B, recvbuffer[expectedseqnum]);
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      }
    }
  }

  ackpkt.seqnum = 0;
  ackpkt.acknum = packet.seqnum;
  for (i = 0; i < 20; i++)
    ackpkt.payload[i] = 0;
  ackpkt.checksum = ComputeChecksum(ackpkt);
  tolayer3(B, ackpkt);
}

void B_init(void) {
  int i;
  expectedseqnum = 0;
  for (i = 0; i < SEQSPACE; i++) {
    received[i] = false;
  }
}

void B_output(struct msg message) { }
void B_timerinterrupt(void) { }
