#ifndef CASREV_SERIAL_H
#define CASREV_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *str);

/* COM2: host koprusu / internet arama kanali */
void serial2_init(void);
int  serial2_present(void);
void serial2_putc(char c);
void serial2_write(const char *str);
int  serial2_read(void);

#endif
