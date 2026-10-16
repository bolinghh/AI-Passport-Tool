#pragma once
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueueOverwrite(QueueHandle_t,const void *);
void vQueueDelete(QueueHandle_t);
