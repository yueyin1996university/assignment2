#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define WINDOW_SIZE 6
#define SEQSPACE 12
#define RTT 16.0
#define NOTINUSE (-1)

struct pkt send_buffer[SEQSPACE];
int send_base = 0;
int next_seqnum = 0;
int recv_base = 0;
int acked[SEQSPACE];
int received[SEQSPACE];
struct pkt recv_buffer[SEQSPACE];

int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    int i;
    checksum += packet.seqnum + packet.acknum;
    for (i = 0; i < 20; i++) {
        checksum += (int)packet.payload[i];
    }
    return checksum;
}

int IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

/******************* A side *******************/
void A_output(struct msg message) {
    struct pkt packet;
    int i;

    if (((next_seqnum - send_base + SEQSPACE) % SEQSPACE) < WINDOW_SIZE) {
        packet.seqnum = next_seqnum;
        packet.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) {
            packet.payload[i] = message.data[i];
        }
        packet.checksum = ComputeChecksum(packet);
        send_buffer[next_seqnum] = packet;

        if (TRACE > 0)
            printf("A_output: sending packet %d\n", packet.seqnum);
        tolayer3(A, packet);
        starttimer(A, RTT);
        next_seqnum = (next_seqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
            printf("A_output: window full, drop message\n");
        window_full++;
    }
}

void A_input(struct pkt packet) {
    int ack;

    if (!IsCorrupted(packet)) {
        ack = packet.acknum;
        if (!acked[ack]) {
            acked[ack] = 1;
            if (TRACE > 0)
                printf("A_input: ACK %d marked as received\n", ack);
            new_ACKs++;
            total_ACKs_received++;
        }

        while (acked[send_base]) {
            acked[send_base] = 0;
            send_base = (send_base + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (send_base != next_seqnum) {
            starttimer(A, RTT);
        }
    } else {
        if (TRACE > 0)
            printf("A_input: corrupted ACK\n");
    }
}

void A_timerinterrupt(void) {
    int i;

    if (TRACE > 0)
        printf("A_timerinterrupt: timeout occurred, retransmitting window\n");

    for (i = 0; i < WINDOW_SIZE; i++) {
        int index = (send_base + i) % SEQSPACE;
        if (((next_seqnum - send_base + SEQSPACE) % SEQSPACE) > i && !acked[index]) {
            if (TRACE > 0)
                printf("A_timerinterrupt: retransmitting packet %d\n", index);
            tolayer3(A, send_buffer[index]);
            packets_resent++;
        }
    }
    starttimer(A, RTT);
}

void A_init(void) {
    int i;
    send_base = 0;
    next_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        acked[i] = 0;
    }
}

/******************* B side *******************/
void B_input(struct pkt packet) {
    struct pkt ack_pkt;
    int i, seq;

    seq = packet.seqnum;

    if (!IsCorrupted(packet) && ((seq - recv_base + SEQSPACE) % SEQSPACE) < WINDOW_SIZE) {
        if (!received[seq]) {
            received[seq] = 1;
            recv_buffer[seq] = packet;
            packets_received++;
            if (TRACE > 0)
                printf("B_input: packet %d received and buffered\n", seq);

            while (received[recv_base]) {
                tolayer5(B, recv_buffer[recv_base].payload);
                received[recv_base] = 0;
                recv_base = (recv_base + 1) % SEQSPACE;
            }
        }
    } else {
        if (TRACE > 0)
            printf("B_input: corrupted or out-of-window packet %d\n", seq);
    }

    ack_pkt.seqnum = 0;
    ack_pkt.acknum = seq;
    for (i = 0; i < 20; i++)
        ack_pkt.payload[i] = '0';
    ack_pkt.checksum = ComputeChecksum(ack_pkt);

    tolayer3(B, ack_pkt);
    if (TRACE > 0)
        printf("B_input: ACK %d sent\n", ack_pkt.acknum);
}

void B_init(void) {
    int i;
    recv_base = 0;
    for (i = 0; i < SEQSPACE; i++) {
        received[i] = 0;
    }
}
