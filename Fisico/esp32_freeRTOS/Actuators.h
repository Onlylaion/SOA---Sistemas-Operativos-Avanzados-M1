#ifndef PUERTA_ACTUATORS_H
#define PUERTA_ACTUATORS_H

#include "Definition.h"

void clearPasskeypad()
{
  memset(passKeypad, 0, sizeof(passKeypad));
  idxPassKeypad = 0;
}

void mostrar_pantalla_reposo()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ingrese Clave:");
}

void detener_timer_teclado()
{
  xTimerStop(timerInactividadTeclado, 0);
  // Vacía el semáforo por si quedó un timeout pendiente sin consumir
  xSemaphoreTake(semTimeoutTeclado, 0);
}

void motor_abrir()
{
  analogWrite(IN2, 0);
  analogWrite(IN1, VELOCIDAD_MOTOR);
}

void motor_cerrar()
{
  analogWrite(IN1, 0);
  analogWrite(IN2, VELOCIDAD_MOTOR);
}

void motor_detener()
{
  analogWrite(IN1, 0);
  analogWrite(IN2, 0);   // ambos en 0 = el motor queda en punto muerto
}

#endif // PUERTA_ACTUATORS_H
