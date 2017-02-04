/*
 * kbd.h
 *
 *  Created on: 21.01.2017
 *      Author: siro
 */

#ifndef __I8042_KBC_H_
#define __I8042_KBC_H_

unsigned char kbc_has_ps2(void);
unsigned char kbc_has_aux(void);

int kbc_probe(void);
int kbc_cmd(unsigned char cmd, unsigned char response);
void kbc_write_input(unsigned char data);

int kbc_data_ready_kb(void);
int kbc_data_ready_mo(void);

unsigned char kbc_data_get_kb(void);
unsigned char kbc_data_get_mo(void);

int kbc_wait_read_kb(void);
int kbc_wait_read_mo(void);


#endif /* __I8042_KBC_H_ */
