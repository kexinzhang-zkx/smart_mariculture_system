#ifndef __DATA_PROCESSOR_H
#define __DATA_PROCESSOR_H
#include "gateway.h"
void  ringbuf_init(RingBuffer *rb);
int   data_validate(SensorData *d);
void *data_process_thread(void *arg);
#endif
