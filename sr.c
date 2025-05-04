#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"

#define SR_WINDOWSIZE 5
#define SR_SEQSPACE 8
#define TIMEOUT 15.0

// Sender side
static struct pkt window[SR_WINDOWSIZE];
static bool acked[SR_WINDOWSIZE];
static int windowfirst = 0;
static int windowcount = 0;
static int nextseqnum = 0;

// Receiver side
static bool received[SR_SEQSPACE];
static struct msg recvbuffer[SR_SEQSPACE];
static int expectedseqnum = 0;

int ComputeChecksum(struct pkt *packet) {
    int checksum = 0;
    checksum += packet->seqnum;
    checksum += packet->acknum;
    for (int i = 0; i < 20; i++)
        checksum += packet->payload[i];
    return checksum;
}

// A-side
void A_output(struct msg message) {
    if (windowcount >= SR_WINDOWSIZE) {
        printf("----A: window full, message dropped\n");
        return;
    }

    struct pkt packet;
    packet.seqnum = nextseqnum;
    packet.acknum = 0;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(&packet);

    int index = (windowfirst + windowcount) % SR_WINDOWSIZE;
    window[index] = packet;
    acked[index] = false;
    windowcount++;

    printf("Sending packet %d to layer 3\n", packet.seqnum);
    tolayer3(0, packet);

    if (windowcount == 1)
        starttimer(0, TIMEOUT);

    nextseqnum = (nextseqnum + 1) % SR_SEQSPACE;
}

void A_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("----A: corrupted ACK received, ignored\n");
        return;
    }

    int ack = packet.acknum;

    // Check if ack is in the window
    for (int i = 0; i < windowcount; i++) {
        int idx = (windowfirst + i) % SR_SEQSPACE;
        if (window[idx].seqnum == ack && !acked[idx % SR_WINDOWSIZE]) {
            printf("----A: ACK %d marked as received\n", ack);
            acked[idx % SR_WINDOWSIZE] = true;

            while (acked[windowfirst % SR_WINDOWSIZE] && windowcount > 0) {
                acked[windowfirst % SR_WINDOWSIZE] = false;
                windowfirst = (windowfirst + 1) % SR_SEQSPACE;
                windowcount--;
            }

            if (windowcount == 0)
                stoptimer(0);
            else
                starttimer(0, TIMEOUT);
            return;
        }
    }

    printf("----A: ACK out of window, ignored\n");
}

void A_timerinterrupt() {
    printf("----A: timeout occurred, retransmitting window\n");
    for (int i = 0; i < windowcount; i++) {
        int idx = (windowfirst + i) % SR_WINDOWSIZE;
        printf("----A: retransmitting packet %d\n", window[idx].seqnum);
        tolayer3(0, window[idx]);
    }
    starttimer(0, TIMEOUT);
}

void A_init() {
    windowfirst = 0;
    windowcount = 0;
    nextseqnum = 0;
    for (int i = 0; i < SR_WINDOWSIZE; i++) {
        acked[i] = false;
    }
}

// B-side
void B_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("----B: corrupted packet received, ignored\n");
        return;
    }

    int seq = packet.seqnum;

    if (!received[seq]) {
        printf("----B: packet %d is correctly received, send ACK!\n", seq);
        received[seq] = true;
        memcpy(recvbuffer[seq].data, packet.payload, 20);
        if (!received[seq]) {
          received[seq] = true;
          memcpy(recvbuffer[seq].data, packet.payload, 20);
          tolayer5(1, recvbuffer[seq].data);  // only once
      }
      
    }

    // Send ACK
    struct pkt ack_pkt;
    ack_pkt.seqnum = 0;
    ack_pkt.acknum = seq;
    memset(ack_pkt.payload, 0, 20);
    ack_pkt.checksum = ComputeChecksum(&ack_pkt);
    tolayer3(1, ack_pkt);
}

void B_init() {
    expectedseqnum = 0;
    for (int i = 0; i < SR_SEQSPACE; i++) {
        received[i] = false;
    }
}

void B_output(struct msg message) {
    // Not used
}

void B_timerinterrupt() {
    // Not used
}
