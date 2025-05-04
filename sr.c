#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h> /* for memcpy */
#include "emulator.h"
#include "gbn.h"

#define RTT  16.0
#define SR_WINDOWSIZE 4
#define SR_SEQSPACE 8 /* must be >= 2 * window size */
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

static int windowfirst, windowlast;
static int windowcount;
static bool acked[SR_WINDOWSIZE];
static int A_nextseqnum;
static struct pkt buffer[SR_WINDOWSIZE];
static struct pkt recv_buffer[SR_SEQSPACE];
static bool received[SR_SEQSPACE];
static int total_retransmissions = 0;
static int expectedseqnum;

void A_output(struct msg message) {
  struct pkt sendpkt;
  int i;
  if (windowcount < SR_WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowlast + 1) % SR_WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false;
    windowcount++;

    if (TRACE > 0) printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);
    if (windowcount == 1) starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SR_SEQSPACE;
  } else {
    if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

void A_input(struct pkt packet) {
  int i;
  if (!IsCorrupted(packet)) {
    if (TRACE > 0) printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      int seqfirst = buffer[windowfirst].seqnum;
      int seqlast = buffer[windowlast].seqnum;
      if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
          ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {

        if (TRACE > 0) printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;

        for (i = 0; i < windowcount; i++) {
          int index = (windowfirst + i) % SR_WINDOWSIZE;
          if (buffer[index].seqnum == packet.acknum) {
            acked[index] = true;
            if (TRACE > 0) printf("----A: ACK %d marked as received\n", packet.acknum);
            break;
          }
        }

        while (acked[windowfirst]) {
          acked[windowfirst] = false;
          windowfirst = (windowfirst + 1) % SR_WINDOWSIZE;
          windowcount--;
          stoptimer(A);
          if (windowcount > 0) starttimer(A, RTT);
        }
      } else {
        if (TRACE > 0) printf("----A: duplicate ACK received, do nothing!\n");
      }
    }
  } else {
    if (TRACE > 0) printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void) {
  int i;
  if (TRACE > 0) printf("----A: timeout occurred, retransmitting first unacked packet\n");
  for (i = 0; i < windowcount; i++) {
    int index = (windowfirst + i) % SR_WINDOWSIZE;
    if (!acked[index]) {
      tolayer3(A, buffer[index]);
      total_retransmissions++;
      if (TRACE > 0) printf("----A: retransmitting packet %d\n", buffer[index].seqnum);
      break;
    }
  }
  starttimer(A, RTT);
}

void A_init(void) {
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < SR_WINDOWSIZE; i++) acked[i] = false;
}

void B_init(void) {
  int i;
  expectedseqnum = 0;
  for (i = 0; i < SR_SEQSPACE; i++) received[i] = false;
}

void B_input(struct pkt packet) {
  int pkt_seq;
  int in_window;
  struct pkt ack_pkt;

  if (IsCorrupted(packet)) {
    if (TRACE > 0) printf("----B: corrupted packet received, ignored\n");
    return;
  }

  pkt_seq = packet.seqnum;
  in_window = ((pkt_seq >= expectedseqnum) && (pkt_seq < expectedseqnum + SR_WINDOWSIZE)) ||
              ((expectedseqnum + SR_WINDOWSIZE >= SR_SEQSPACE) &&
               ((pkt_seq < (expectedseqnum + SR_WINDOWSIZE) % SR_SEQSPACE) || pkt_seq >= expectedseqnum));

  if (in_window) {
    if (!received[pkt_seq]) {
      received[pkt_seq] = true;
      recv_buffer[pkt_seq] = packet;
      if (TRACE > 0) printf("----B: packet %d buffered\n", pkt_seq);
    } else {
      if (TRACE > 0) printf("----B: duplicate packet %d received, already buffered\n", pkt_seq);
    }

    ack_pkt.acknum = pkt_seq;
    ack_pkt.seqnum = 0;
    memcpy(ack_pkt.payload, "ACK", 4);
    ack_pkt.checksum = ComputeChecksum(ack_pkt);
    tolayer3(B, ack_pkt);

    while (received[expectedseqnum]) {
      tolayer5(B, recv_buffer[expectedseqnum].payload);
      received[expectedseqnum] = false;
      expectedseqnum = (expectedseqnum + 1) % SR_SEQSPACE;
    }
  } else {
    if (TRACE > 0) printf("----B: packet %d is outside the receiver window, ignored\n", pkt_seq);
    ack_pkt.acknum = pkt_seq;
    ack_pkt.seqnum = 0;
    memcpy(ack_pkt.payload, "ACK", 4);
    ack_pkt.checksum = ComputeChecksum(ack_pkt);
    tolayer3(B, ack_pkt);
  }
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
