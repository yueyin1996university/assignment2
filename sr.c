#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emulator.h"

#define SR_WINDOWSIZE 5
#define SR_SEQSPACE 10
#define TIMEOUT 20.0

/* Sender side */
static int base;
static int nextseqnum;
static struct pkt window[SR_WINDOWSIZE];
static int acked[SR_SEQSPACE];
static float timer_start = 0.0;

/* Receiver side */
static int expectedseqnum;
static int received[SR_SEQSPACE];
static struct pkt buffer[SR_SEQSPACE];

/* Utility */
int ComputeChecksum(struct pkt *packet) {
    int checksum = packet->seqnum + packet->acknum;
    int i;
    for (i = 0; i < 20; i++) {
        checksum += packet->payload[i];
    }
    return checksum;
}

void A_output(struct msg message) {
    if ((nextseqnum + SR_SEQSPACE - base) % SR_SEQSPACE >= SR_WINDOWSIZE) {
        printf("----A: window is full, drop message\n");
        return;
    }

    struct pkt packet;
    int i;
    for (i = 0; i < 20; i++)
        packet.payload[i] = message.data[i];

    packet.seqnum = nextseqnum;
    packet.acknum = 0;
    packet.checksum = ComputeChecksum(&packet);

    window[(nextseqnum - base + SR_SEQSPACE) % SR_SEQSPACE] = packet;
    acked[packet.seqnum] = 0;

    tolayer3(0, packet);
    printf("Sending packet %d to layer 3\n", packet.seqnum);

    if (base == nextseqnum)
        starttimer(0, TIMEOUT);

    nextseqnum = (nextseqnum + 1) % SR_SEQSPACE;
}

void A_input(struct pkt packet) {
    int checksum = ComputeChecksum(&packet);
    if (checksum != packet.checksum) {
        printf("----A: corrupted ACK, ignored\n");
        return;
    }

    int ack = packet.acknum;
    if (acked[ack]) {
        printf("----A: duplicate ACK, ignored\n");
        return;
    }

    printf("----A: ACK %d marked as received\n", ack);
    acked[ack] = 1;

    while (acked[base]) {
        base = (base + 1) % SR_SEQSPACE;
    }

    if (base == nextseqnum)
        stoptimer(0);
    else
        starttimer(0, TIMEOUT);
}

void A_timerinterrupt() {
    printf("----A: timeout occurred, retransmitting window\n");
    starttimer(0, TIMEOUT);

    int i;
    for (i = 0; i < SR_WINDOWSIZE; i++) {
        int index = (base + i) % SR_SEQSPACE;
        if ((nextseqnum - base + SR_SEQSPACE) % SR_SEQSPACE > i && !acked[index]) {
            tolayer3(0, window[index]);
            printf("----A: retransmitting packet %d\n", window[index].seqnum);
        }
    }
}

void A_init() {
    int i;
    base = 0;
    nextseqnum = 0;
    for (i = 0; i < SR_SEQSPACE; i++) {
        acked[i] = 0;
    }
}

/* Receiver */
void B_input(struct pkt packet) {
    int checksum = ComputeChecksum(&packet);
    if (checksum != packet.checksum) {
        printf("----B: corrupted packet, ignored\n");
        return;
    }

    int seq = packet.seqnum;
    if (!received[seq]) {
        printf("----B: packet %d is correctly received, send ACK!\n", seq);
        buffer[seq] = packet;
        received[seq] = 1;
    }

    while (received[expectedseqnum]) {
        tolayer5(1, buffer[expectedseqnum].payload);
        received[expectedseqnum] = 0;
        expectedseqnum = (expectedseqnum + 1) % SR_SEQSPACE;
    }

    struct pkt ack_pkt;
    ack_pkt.seqnum = 0;
    ack_pkt.acknum = seq;
    ack_pkt.checksum = ComputeChecksum(&ack_pkt);
    tolayer3(1, ack_pkt);
}

void B_output(struct msg message) {
    /* Unused */
}

void B_timerinterrupt() {
    /* Unused */
}

void B_init() {
    int i;
    expectedseqnum = 0;
    for (i = 0; i < SR_SEQSPACE; i++) {
        received[i] = 0;
    }
}
