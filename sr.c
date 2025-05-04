#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

struct pkt buffer[SEQSPACE];
bool received[SEQSPACE];
bool acked[SEQSPACE];
bool sent[SEQSPACE];
int base = 0;
int nextseqnum = 0;
int expectedseqnum = 0;

int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;
  checksum = packet.seqnum + packet.acknum;
  for (i = 0; i < 20; i++)
    checksum += (int)(packet.payload[i]);
  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  return packet.checksum != ComputeChecksum(packet);
}

void A_output(struct msg message)
{
  struct pkt packet;
  int i;
  if (((nextseqnum - base + SEQSPACE) % SEQSPACE) < WINDOWSIZE)
  {
    packet.seqnum = nextseqnum;
    packet.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      packet.payload[i] = message.data[i];
    packet.checksum = ComputeChecksum(packet);
    buffer[nextseqnum] = packet;
    sent[nextseqnum] = true;
    acked[nextseqnum] = false;
    tolayer3(A, packet);
    if (base == nextseqnum)
      starttimer(A, RTT);
    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  }
  else
  {
    if (TRACE > 0)
      printf("----A: window full, message dropped!\n");
    window_full++;
  }
}

void A_input(struct pkt packet)
{
  if (!IsCorrupted(packet))
  {
    if (!acked[packet.acknum])
    {
      acked[packet.acknum] = true;
      new_ACKs++;
      total_ACKs_received++;
      while (acked[base])
      {
        base = (base + 1) % SEQSPACE;
      }
      stoptimer(A);
      if (base != nextseqnum)
        starttimer(A, RTT);
    }
  }
}

void A_timerinterrupt(void)
{
  int i;
  if (TRACE > 0)
    printf("----A: timeout occurred, retransmitting window\n");
  for (i = 0; i < SEQSPACE; i++)
  {
    if (sent[i] && !acked[i] && ((i - base + SEQSPACE) % SEQSPACE) < WINDOWSIZE)
    {
      tolayer3(A, buffer[i]);
      packets_resent++;
    }
  }
  starttimer(A, RTT);
}

void A_init(void)
{
  int i;
  for (i = 0; i < SEQSPACE; i++)
  {
    acked[i] = false;
    sent[i] = false;
  }
  base = 0;
  nextseqnum = 0;
}

void B_input(struct pkt packet)
{
  struct pkt ack_pkt;
  int i;
  if (!IsCorrupted(packet) && ((packet.seqnum - expectedseqnum + SEQSPACE) % SEQSPACE) < WINDOWSIZE)
  {
    if (!received[packet.seqnum])
    {
      received[packet.seqnum] = true;
      for (i = 0; i < 20; i++)
        buffer[packet.seqnum].payload[i] = packet.payload[i];
      buffer[packet.seqnum].seqnum = packet.seqnum;
      buffer[packet.seqnum].acknum = NOTINUSE;
      buffer[packet.seqnum].checksum = packet.checksum;
    }
    while (received[expectedseqnum])
    {
      tolayer5(B, buffer[expectedseqnum].payload);
      packets_received++;
      received[expectedseqnum] = false;
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }
  }
  ack_pkt.seqnum = 0;
  ack_pkt.acknum = packet.seqnum;
  for (i = 0; i < 20; i++)
    ack_pkt.payload[i] = '0';
  ack_pkt.checksum = ComputeChecksum(ack_pkt);
  tolayer3(B, ack_pkt);
}

void B_init(void)
{
  int i;
  for (i = 0; i < SEQSPACE; i++)
  {
    received[i] = false;
  }
  expectedseqnum = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
