#ifndef PUERTA_SENSORS_H
#define PUERTA_SENSORS_H

#include "Definition.h"

events verificarEstadoSensorKeypad()
{
  events new_event = EV_CONT;
  char tecla = keypad.getKey();

  if (tecla != NO_KEY)
  {
    if (tecla == '#')
    {
      new_event = (strcmp(passKeypad, PASS_TEST) == 0)
                    ? EV_CONTRASENIA_VALIDA : EV_CONTRASENIA_INVALIDA;
    }
    else if (tecla == '*')
    {
      new_event = EV_CANCELAR_TECLADO;
    }
    else
    {
      ultimo_caracter = tecla;
      new_event = EV_INGRESO_TECLA;
    }
  }
  return new_event;
}

events verificarFinCarrera()
{
  events new_event = EV_CONT;
  static int previo_green = HIGH;
  static int previo_red   = HIGH;

  int actual_green = digitalRead(GREEN_PULSADOR);
  int actual_red   = digitalRead(RED_PULSADOR);

  if (actual_green != previo_green)
  {
    previo_green = actual_green;
    if (actual_green == LOW) new_event = EV_FIN_CARRERA_ABIERTO;
  }

  if (actual_red != previo_red)
  {
    previo_red = actual_red;
    if (actual_red == LOW) new_event = EV_FIN_CARRERA_CERRADO;
  }
  return new_event;
}

events verificarEstadoSensorTouch()
{
  events new_event = EV_CONT;
  bool tocado = (touchRead(TOUCH_PIN) < UMBRAL_TOUCH);

  if (tocado && !touch_mantenido)
  {
    touch_mantenido = true;          // recién empieza el toque
    new_event = EV_TOUCH_DETECTADO;
  }
  else if (!tocado && touch_mantenido)
  {
    touch_mantenido = false;         // se rearma para el próximo toque
  }
  return new_event;
}

#endif // PUERTA_SENSORS_H
