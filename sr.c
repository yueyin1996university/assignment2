#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h> /*  for memcpy */
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Selective Repeat protocol. Adapted from GBN and textbook example
**********************************************************************/

#define RTT  16.0               /* round trip time for timer */
#define SR_WINDOWSIZE 4
#define SR_SEQSPACE 8           /* must be >= 2 * window size */
#define NOTINUSE (-1)           /* unused field value */

int ComputeChecksum(struct pkt packet)
{
    int i;

  int checfor (i = 0; i < 20; i++)i < 20; i++)
    checksum += (int)(packet.payload[i]);
  checksum += packet.seqnum;
  checksum += packet.acknum;
  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  return packet.checksum != ComputeChecksum(packet);
}

/********* Sender (A) variables and functions ************/

static int windowfirst, windowlast;
static int windowcount;
static bool acked[SR_WINDOWSIZE];     /*  ✅ track which packets have been ACKed */
static int A_nextseqnum;              /*  ✅ next sequence number to use */
static struct pkt buffer[SR_WINDOWSIZE]; /*  ✅ sent packet buffer */

/* called from layer 5 */
void A_output(struct msg message)
{
  struct pkt sendpkt;

  if (windowcount < SR_WINDOWSIZE) {
    int i;

    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOfor (i = 0; i < 20; i++)i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowlast + 1) % SR_WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false;
    windowcount++;

    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    if (windowcount == 1)
      starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SR_SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

void A_input(struct pkt packet)
{
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      int seqfirst = buffer[windowfirst].seqnum;
      int seqlast = buffer[windowlast].seqnum;

      if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
          ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {
    int i;


        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs+for (i = 0; i < windowcount; i++)wcount; i++) {
          int index = (windowfirst + i) % SR_WINDOWSIZE;
          if (buffer[index].seqnum == packet.acknum) {
            acked[index] = true;
            if (TRACE > 0)
              printf("----A: ACK %d marked as received\n", packet.acknum);
            break;
          }
        }

        while (acked[windowfirst]) {
          acked[windowfirst] = false;
          windowfirst = (windowfirst + 1) % SR_WINDOWSIZE;
          windowcount--;
          stoptimer(A);
          if (windowcount > 0)
            starttimer(A, RTT);
        }
      } else {
        if (TRACE > 0)
          printf("----A: duplicate ACK received, do nothing!\n");
      }
    }
  } else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void)
{
    int i;

  if (TRACE > 0)
    printf("----A: timeout occurred, retransmitting first unacked pacfor (i = 0; i < windowcount; i++)wcount; i++) {
    int index = (windowfirst + i) % SR_WINDOWSIZE;
    if (!acked[index]) {
      tolayer3(A, buffer[index]);
      if (TRACE > 0)
        printf("----A: retransmitting packet %d\n", buffer[index].seqnum);
      break;
    }
  }
  starttimer(A, RTT);
}

void A_init(void)
{
    int i;

  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcfor (i = 0; i < SR_WINDOWSIZE; i++)OWSIZE; i++) acked[i] = false;
}

/********* Receiver (B) variables and functions ************/

static int expectedseqnum;
static struct pkt recv_buffer[SR_SEQSPACE];     /*  ✅ buffer for received packets */
static bool received[SR_SEQSPACE];              /*  ✅ flags for seqnums */

void B_input(struct pkt packet)
{
  if (IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: corrupted packet received, ignored\n");
    return;
  }

  int pkt_seq = packet.seqnum;
  bool in_window = ((pkt_seq >= expectedseqnum) && (pkt_seq < expectedseqnum + SR_WINDOWSIZE)) ||
                   ((expectedseqnum + SR_WINDOWSIZE >= SR_SEQSPACE) &&
                    ((pkt_seq < (expectedseqnum + SR_WINDOWSIZE) % SR_SEQSPACE) || pkt_seq >= expectedseqnum));

  if (in_window) {
    if (!received[pkt_seq]) {
      received[pkt_seq] = true;
      recv_buffer[pkt_seq] = packet;
      if (TRACE > 0)
        printf("----B: packet %d buffered\n", pkt_seq);
    } else {
      if (TRACE > 0)
        printf("----B: duplicate packet %d received, already buffered\n", pkt_seq);
    }

    struct pkt ack_pkt;
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
    if (TRACE > 0)
      printf("----B: packet %d is outside the receiver window, ignored\n", pkt_seq);

    struct pkt ack_pkt;
    ack_pkt.acknum = pkt_seq;
    ack_pkt.seqnum = 0;
    memcpy(ack_pkt.payload, "ACK", 4);
    ack_pkt.checksum = ComputeChecksum(ack_pkt);
    tolayer3(B, ack_pkt);
  }
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}

void B_init(void)
{
    int i;

  expectedsefor (i = 0; i < SR_SEQSPACE; i++)QSPACE; i++)
    received[i] = false;
}
