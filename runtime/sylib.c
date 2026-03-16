#include "sylib.h"

#include <stdio.h>
#include <sys/time.h>

static struct timeval g_start;
static struct timeval g_end;

int getint(void) {
  int x = 0;
  scanf("%d", &x);
  return x;
}

int getch(void) {
  char c = 0;
  scanf("%c", &c);
  return (int)c;
}

float getfloat(void) {
  float x = 0.0f;
  scanf("%f", &x);
  return x;
}

int getarray(int a[]) {
  int n = getint();
  for (int i = 0; i < n; ++i) {
    a[i] = getint();
  }
  return n;
}

int getfarray(float a[]) {
  int n = getint();
  for (int i = 0; i < n; ++i) {
    a[i] = getfloat();
  }
  return n;
}

void putint(int a) {
  printf("%d", a);
}

void putch(int a) {
  printf("%c", a);
}

void putfloat(float a) {
  printf("%f", a);
}

void putarray(int n, int a[]) {
  printf("%d:", n);
  for (int i = 0; i < n; ++i) {
    printf(" %d", a[i]);
  }
  printf("\n");
}

void putfarray(int n, float a[]) {
  printf("%d:", n);
  for (int i = 0; i < n; ++i) {
    printf(" %f", a[i]);
  }
  printf("\n");
}

void _sysy_starttime(int lineno) {
  (void)lineno;
  gettimeofday(&g_start, NULL);
}

void _sysy_stoptime(int lineno) {
  (void)lineno;
  gettimeofday(&g_end, NULL);
  long us = (g_end.tv_sec - g_start.tv_sec) * 1000000L + (g_end.tv_usec - g_start.tv_usec);
  fprintf(stderr, "TOTAL: %ld us\n", us);
}
