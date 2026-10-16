#ifndef PUERTA_TIMERS_H
#define PUERTA_TIMERS_H

#include "Definition.h"


void callbackTimeoutTeclado(TimerHandle_t xTimer)
{
  xSemaphoreGive(semTimeoutTeclado);
}

events verificarTimeoutKeypad()
{
  events new_event = EV_CONT;
  // Toma no bloqueante (timeout 0)
  if (xSemaphoreTake(semTimeoutTeclado, 0) == pdTRUE)
  {
    new_event = EV_TIMEOUT_TECLADO;
  }
  return new_event;
}

events verificarTimeoutPuerta()
{
  events new_event = EV_CONT;

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  bool activo = timer_puerta_activo;
  unsigned long inicio = lct;
  xSemaphoreGive(mutexTimersCompartidos);

  if (activo && (millis() - inicio >= TIEMPO_PUERTA_ABIERTA))
  {
    new_event = EV_TIMEOUT_PUERTA;
  }
  return new_event;
}

events verificarTimeoutError()
{
  events new_event = EV_CONT;

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  bool hayError = mostrando_error;
  unsigned long inicio = error_timer;
  xSemaphoreGive(mutexTimersCompartidos);

  if (hayError && (millis() - inicio >= TIEMPO_ERROR))
  {
    new_event = EV_TIMEOUT_ERROR;
  }
  return new_event;
}

#endif // PUERTA_TIMERS_H
