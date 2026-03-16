#ifndef SYSYLIB_H
#define SYSYLIB_H

int getint(void);
int getch(void);
float getfloat(void);
int getarray(int a[]);
int getfarray(float a[]);
void putint(int a);
void putch(int a);
void putfloat(float a);
void putarray(int n, int a[]);
void putfarray(int n, float a[]);
void _sysy_starttime(int lineno);
void _sysy_stoptime(int lineno);

#endif
