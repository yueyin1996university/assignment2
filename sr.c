#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define SR_WINDOWSIZE 4
#define SR_SEQSPACE 8 // must be >= 2 * window size 
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static bool acked[SR_WINDOWSIZE];     // tracks individual ACKs
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static struct pkt buffer[SR_WINDOWSIZE]; // already used for sender
static struct pkt recv_buffer[SR_SEQSPACE];
static bool received[SR_SEQSPACE];
static int total_retransmissions = 0;
static int expectedseqnum;  // expected sequence number at receiver (B)




void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if (windowcount < SR_WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % SR_WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false;  // ✅ ensure acked[] is reset
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A, RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SR_SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}



void A_input(struct pkt packet)
{
  int i;

  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      int seqfirst = buffer[windowfirst].seqnum;
      int seqlast = buffer[windowlast].seqnum;

      /* check if ACK is within sender window (handles wrap-around) */
      if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
          ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {

        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;

        /* mark matching packet as ACKed */
        for (i = 0; i < windowcount; i++) {
          int index = (windowfirst + i) % SR_WINDOWSIZE;
          if (buffer[index].seqnum == packet.acknum) {
            acked[index] = true;
            if (TRACE > 0)
              printf("----A: ACK %d marked as received\n", packet.acknum);
            break;
          }
        }

        /* slide window forward only for in-order ACKs */
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
  if (TRACE > 0)
    printf("----A: timeout occurred, retransmitting first unacked packet\n");

  // Find the first unACKed packet in the window
  for (int i = 0; i < windowcount; i++) {
    int index = (windowfirst + i) % SR_WINDOWSIZE;
    if (!acked[index]) {
      tolayer3(A, buffer[index]);
      total_retransmissions++;
      if (TRACE > 0)
        printf("----A: retransmitting packet %d\n", buffer[index].seqnum);
      break;
    }
  }

  // Restart the timer
  starttimer(A, RTT);
}





void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;

  // NEW: initialize acked array for Selective Repeat
  for (int i = 0; i < SR_WINDOWSIZE; i++) {
    acked[i] = false;
  }
}

void B_init(void)
{
  expectedseqnum = 0;
  for (int i = 0; i < SR_SEQSPACE; i++)
    received[i] = false;
}



/********* Receiver (B)  variables and procedures ************/


void B_input(struct pkt packet)
{
  if (IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: corrupted packet received, ignored\n");
    return;
  }

  int pkt_seq = packet.seqnum;

  /* Check if within receiver window */
  int in_window = ((pkt_seq >= expectedseqnum) && (pkt_seq < expectedseqnum + SR_WINDOWSIZE)) ||
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

    // Send ACK for every received packet
    struct pkt ack_pkt;
    ack_pkt.acknum = pkt_seq;
    ack_pkt.seqnum = 0; // not used
    memcpy(ack_pkt.payload, "ACK", 4);
    ack_pkt.checksum = ComputeChecksum(ack_pkt);
    tolayer3(B, ack_pkt);

    // Deliver in-order packets to layer 5
    while (received[expectedseqnum]) {
      tolayer5(B, recv_buffer[expectedseqnum].payload);
      received[expectedseqnum] = false;
      expectedseqnum = (expectedseqnum + 1) % SR_SEQSPACE;
    }
  } else {
    if (TRACE > 0)
      printf("----B: packet %d is outside the receiver window, ignored\n", pkt_seq);

    // Still send ACK to help sender
    struct pkt ack_pkt;
    ack_pkt.acknum = pkt_seq;
    ack_pkt.seqnum = 0;
    memcpy(ack_pkt.payload, "ACK", 4);
    ack_pkt.checksum = ComputeChecksum(ack_pkt);
    tolayer3(B, ack_pkt);
  }
}


/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
