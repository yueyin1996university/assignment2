#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define WINDOW_SIZE 10
#define TIMEOUT 20.0

static int base = 0, nextseqnum = 0;
static struct pkt sndpkt[WINDOW_SIZE];
static int acked[WINDOW_SIZE];

static int recv_base = 0;
static struct pkt recv_buffer[WINDOW_SIZE];
static int received[WINDOW_SIZE];

int compute_checksum(struct pkt *packet) {
    int checksum = packet->seqnum + packet->acknum;
    int i;
    for (i = 0; i < 20; ++i) {
        checksum += packet->payload[i];
    }
    return checksum;
}

void A_output(struct msg message) {
    if (nextseqnum < base + WINDOW_SIZE) {
        struct pkt *packet = &sndpkt[nextseqnum % WINDOW_SIZE];
        int i;
        packet->seqnum = nextseqnum;
        packet->acknum = 0;
        for (i = 0; i < 20; ++i)
            packet->payload[i] = message.data[i];
        packet->checksum = compute_checksum(packet);

        tolayer3(0, *packet);

        acked[nextseqnum % WINDOW_SIZE] = 0;
        if (base == nextseqnum)
            starttimer(0, TIMEOUT);

        nextseqnum++;
    }
}

void A_input(struct pkt pkt) {
    if (compute_checksum(&pkt) == pkt.checksum) {
        acked[pkt.acknum % WINDOW_SIZE] = 1;
        while (acked[base % WINDOW_SIZE]) {
            acked[base % WINDOW_SIZE] = 0;
            base++;
        }
        if (base == nextseqnum)
            stoptimer(0);
        else
            starttimer(0, TIMEOUT);
    }
}

void A_timerinterrupt() {
    int i;
    starttimer(0, TIMEOUT);
    for (i = base; i < nextseqnum; ++i) {
        if (!acked[i % WINDOW_SIZE])
            tolayer3(0, sndpkt[i % WINDOW_SIZE]);
    }
}

void A_init() {
    base = nextseqnum = 0;
    memset(acked, 0, sizeof(acked));
}

void B_input(struct pkt pkt) {
    if (compute_checksum(&pkt) == pkt.checksum) {
        int seq = pkt.seqnum;
        if (seq >= recv_base && seq < recv_base + WINDOW_SIZE && !received[seq % WINDOW_SIZE]) {
            received[seq % WINDOW_SIZE] = 1;
            recv_buffer[seq % WINDOW_SIZE] = pkt;

            while (received[recv_base % WINDOW_SIZE]) {
                tolayer5(1, recv_buffer[recv_base % WINDOW_SIZE].payload);
                received[recv_base % WINDOW_SIZE] = 0;
                recv_base++;
            }
        }

        /* ACK */
        {
            struct pkt ackpkt;
            int i;
            ackpkt.seqnum = 0;
            ackpkt.acknum = pkt.seqnum;
            for (i = 0; i < 20; ++i)
                ackpkt.payload[i] = 0;
            ackpkt.checksum = compute_checksum(&ackpkt);
            tolayer3(1, ackpkt);
        }
    }
}

void B_init() {
    recv_base = 0;
    memset(received, 0, sizeof(received));
}

void B_output(struct msg message) {
  /* Not used in Selective Repeat */
}

void B_timerinterrupt() {
  /* Not used in Selective Repeat */
}
