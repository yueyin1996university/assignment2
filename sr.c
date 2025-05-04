#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"

#define SR_WINDOWSIZE 5
#define SR_SEQSPACE 8
#define TIMEOUT 15.0

// Sender state
static struct pkt window[SR_WINDOWSIZE];
static bool acked[SR_WINDOWSIZE];
static int base = 0;
static int nextseqnum = 0;
static int windowcount = 0;

// Receiver state
static struct pkt recv_buffer[SR_SEQSPACE];
static bool received[SR_SEQSPACE];
static int expectedseqnum = 0;

int ComputeChecksum(struct pkt *packet) {
    int checksum = 0;
    checksum += packet->seqnum + packet->acknum;
    for (int i = 0; i < 20; i++)
        checksum += packet->payload[i];
    return checksum;
}

// A-side functions
void A_output(struct msg message) {
    if (windowcount >= SR_WINDOWSIZE) {
        printf("----A: window full, message dropped\n");
        return;
    }

    struct pkt packet;
    packet.seqnum = nextseqnum;
    packet.acknum = 0;
    memcpy(packet.payload, message.data, sizeof(packet.payload));
    packet.checksum = ComputeChecksum(&packet);

    int index = (base + windowcount) % SR_WINDOWSIZE;
    window[index] = packet;
    acked[index] = false;
    windowcount++;

    printf("Sending packet %d to layer 3\n", packet.seqnum);
    tolayer3(0, packet);
    if (windowcount == 1) starttimer(0, TIMEOUT);

    nextseqnum = (nextseqnum + 1) % SR_SEQSPACE;
}

void A_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("----A: corrupted ACK received\n");
        return;
    }

    int acknum = packet.acknum;
    for (int i = 0; i < windowcount; i++) {
        int index = (base + i) % SR_WINDOWSIZE;
        if (window[index].seqnum == acknum && !acked[index]) {
            printf("----A: ACK %d received\n", acknum);
            acked[index] = true;
            break;
        }
    }

    while (windowcount > 0 && acked[base % SR_WINDOWSIZE]) {
        acked[base % SR_WINDOWSIZE] = false;
        base = (base + 1) % SR_SEQSPACE;
        windowcount--;
    }

    if (windowcount == 0)
        stoptimer(0);
    else
        starttimer(0, TIMEOUT);
}

void A_timerinterrupt(void) {
    printf("----A: timeout occurred, retransmitting window\n");
    starttimer(0, TIMEOUT);
    for (int i = 0; i < windowcount; i++) {
        int index = (base + i) % SR_WINDOWSIZE;
        printf("----A: retransmitting packet %d\n", window[index].seqnum);
        tolayer3(0, window[index]);
    }
}

void A_init(void) {
    base = nextseqnum = windowcount = 0;
    memset(acked, 0, sizeof(acked));
}

// B-side functions
void B_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("----B: corrupted packet ignored\n");
        return;
    }

    int seq = packet.seqnum;
    printf("----B: packet %d is correctly received\n", seq);

    if (!received[seq]) {
        received[seq] = true;
        recv_buffer[seq] = packet;
    }

    struct pkt ack_pkt;
    ack_pkt.seqnum = 0;
    ack_pkt.acknum = seq;
    memset(ack_pkt.payload, 0, sizeof(ack_pkt.payload));
    ack_pkt.checksum = ComputeChecksum(&ack_pkt);
    tolayer3(1, ack_pkt);

    while (received[expectedseqnum]) {
        tolayer5(1, recv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum + 1) % SR_SEQSPACE;
    }
}

void B_init(void) {
    expectedseqnum = 0;
    memset(received, 0, sizeof(received));
}
