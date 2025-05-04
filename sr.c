#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "emulator.h"

#define SR_WINDOWSIZE 8
#define SR_SEQSPACE 16
#define TIMEOUT 20.0

struct pkt window[SR_WINDOWSIZE];
bool acked[SR_WINDOWSIZE];
int base = 0;
int nextseq = 0;

struct msg recvbuffer[SR_SEQSPACE];
bool received[SR_SEQSPACE];
int expectedseq = 0;

/* Compute checksum for a packet */
int ComputeChecksum(struct pkt *packet) {
    int checksum = packet->seqnum + packet->acknum;
    for (int i = 0; i < 20; i++)
        checksum += packet->payload[i];
    return checksum;
}

/* Check packet validity */
bool IsValid(struct pkt *packet) {
    return packet->checksum == ComputeChecksum(packet);
}

void A_output(struct msg message) {
    if ((nextseq + SR_SEQSPACE - base) % SR_SEQSPACE >= SR_WINDOWSIZE) {
        printf("----A: window full, drop message\n");
        return;
    }

    struct pkt packet;
    packet.seqnum = nextseq;
    packet.acknum = 0;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = ComputeChecksum(&packet);

    int idx = nextseq % SR_WINDOWSIZE;
    window[idx] = packet;
    acked[idx] = false;

    printf("Sending packet %d to layer 3\n", packet.seqnum);
    tolayer3(0, packet);
    if (base == nextseq)
        starttimer(0, TIMEOUT);

    nextseq = (nextseq + 1) % SR_SEQSPACE;
}

void A_input(struct pkt packet) {
    if (!IsValid(&packet))
        return;

    int seq = packet.acknum;
    if ((base <= seq && seq < nextseq) || (nextseq < base && (seq >= base || seq < nextseq))) {
        int idx = seq % SR_WINDOWSIZE;
        if (!acked[idx]) {
            printf("----A: ACK %d marked as received\n", seq);
            acked[idx] = true;

            while (acked[base % SR_WINDOWSIZE]) {
                acked[base % SR_WINDOWSIZE] = false;
                base = (base + 1) % SR_SEQSPACE;
                if (base == nextseq)
                    stoptimer(0);
                else
                    starttimer(0, TIMEOUT);
            }
        }
    } else {
        printf("----A: ACK out of window, ignored\n");
    }
}

void A_timerinterrupt() {
    printf("----A: timeout occurred, retransmitting window\n");
    for (int i = 0; i < SR_WINDOWSIZE; i++) {
        int seq = (base + i) % SR_SEQSPACE;
        if ((nextseq > base && seq < nextseq) || (nextseq < base && (seq >= base || seq < nextseq))) {
            if (!acked[seq % SR_WINDOWSIZE]) {
                printf("----A: retransmitting packet %d\n", seq);
                tolayer3(0, window[seq % SR_WINDOWSIZE]);
            }
        }
    }
    starttimer(0, TIMEOUT);
}

void A_init() {
    base = 0;
    nextseq = 0;
    for (int i = 0; i < SR_WINDOWSIZE; i++)
        acked[i] = false;
}

void B_input(struct pkt packet) {
    if (!IsValid(&packet))
        return;

    int seq = packet.seqnum;
    if (!received[seq]) {
        printf("----B: packet %d is correctly received, send ACK!\n", seq);
        memcpy(recvbuffer[seq].data, packet.payload, 20);
        received[seq] = true;

        struct pkt ack_pkt;
        ack_pkt.seqnum = 0;
        ack_pkt.acknum = seq;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = ComputeChecksum(&ack_pkt);
        tolayer3(1, ack_pkt);

        while (received[expectedseq]) {
            tolayer5(1, recvbuffer[expectedseq].data);
            received[expectedseq] = false;
            expectedseq = (expectedseq + 1) % SR_SEQSPACE;
        }
    } else {
        // Send duplicate ACK
        struct pkt ack_pkt;
        ack_pkt.seqnum = 0;
        ack_pkt.acknum = seq;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = ComputeChecksum(&ack_pkt);
        tolayer3(1, ack_pkt);
    }
}

void B_init() {
    expectedseq = 0;
    for (int i = 0; i < SR_SEQSPACE; i++)
        received[i] = false;
}


void B_output(struct msg message) {
  // Not used in Selective Repeat
}

void B_timerinterrupt() {
  // Not used in Selective Repeat
}
