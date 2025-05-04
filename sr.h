#ifndef SR_H
#define SR_H

#define WINDOW_SIZE 10
#define TIMEOUT 20.0

void A_output(struct msg message);
void A_input(struct pkt pkt);
void A_timerinterrupt();
void A_init();

void B_input(struct pkt pkt);
void B_init();

#endif
