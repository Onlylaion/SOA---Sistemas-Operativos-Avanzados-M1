#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// Definition.h
// ============================================================
#define SERIAL_DEBUG_ENABLED 1
#if SERIAL_DEBUG_ENABLED
  #define DebugPrint(str) { Serial.println(str); }
#else
  #define DebugPrint(str)
#endif

#define DebugPrintEstado(estado, evento) { \
  String est = estado; String evt = evento; String str; \
  str = "EST-> [" + est + "]: EVT-> [" + evt + "]."; \
  DebugPrint(str); \
}

#define TAM_DNI 8
#define PASS_TEST "1234567"
#define TIEMPO_PUERTA_ABIERTA 10000   // ms
#define TIEMPO_ERROR 3000             // ms que se muestra un mensaje temporal
#define TIMEOUT_INGRESO 5000          // ms de inactividad del teclado

// ---- MQTT ----
#define TOPIC_OCUPACION "gimnasio/ocupacion/cantidad"   // Topic donde se publica la cantidad de gente
#define CAPACIDAD_MAXIMA 50                             // Tope físico del gimnasio
int cantidadPersonas = 0;                               // Contador de ocupación actual, arranca en 0

// ---- Buzzer ----
const int BUZZER_PIN = 13;

// ---- Finales de carrera ----
const int GREEN_PULSADOR = 25;   // fin de carrera: puerta abierta
const int RED_PULSADOR   = 26;   // fin de carrera: puerta cerrada

// ---- LEDs ----
const int RED_LED   = 32;
const int GREEN_LED = 33;

// ---- Display I2C ----
const int SCL_PIN = 22;
const int SDA_PIN = 21;
const byte ADDR_DISPLAY = 0x27;
LiquidCrystal_I2C lcd(ADDR_DISPLAY, 16, 2);

// ---- Keypad ----
const uint8_t ROWS = 4;
const uint8_t COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};
uint8_t colPins[COLS] = { 16, 4, 27 };
uint8_t rowPins[ROWS] = { 23, 19, 18, 17 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char passKeypad[TAM_DNI + 1];
int  idxPassKeypad = 0;
char ultimo_caracter = '\0';

// ---- Puente H DRV8833 ----
#define IN1 12
#define IN2 14
#define VELOCIDAD_MOTOR 127 

// ---- Sensor touch capacitivo ----
#define TOUCH_PIN 15
#define UMBRAL_TOUCH 1000        // lectura < UMBRAL_TOUCH => tocado
bool touch_mantenido = false;    // evita disparar el evento en cada lectura mientras

// ---- Timeout de teclado: xTimer + semáforo binario ----
// (antes era un flag volatile; el callback corre en la tarea Timer Service de
//  FreeRTOS y se leía desde v_task_get_new_event sin sincronización)
TimerHandle_t   timerInactividadTeclado = NULL;
SemaphoreHandle_t semTimeoutTeclado = NULL;

// ---- Timeout de puerta: millis() ----
// lct y error_timer las escribe v_loop_task y las lee v_task_get_new_event
// (en verificarTimeoutPuerta / verificarTimeoutError) => se protegen con mutex.
bool timer_puerta_activo = false;
unsigned long lct = 0;
int tiempoRestante = TIEMPO_PUERTA_ABIERTA / 1000;

// ---- Mensaje temporal en pantalla (error / puerta cerrada) ----
bool mostrando_error = false;
unsigned long error_timer = 0;

// Mutex que protege el "paquete" lct/timer_puerta_activo/error_timer/mostrando_error,
// compartido entre v_loop_task (que las escribe) y v_task_get_new_event (que las lee).
SemaphoreHandle_t mutexTimersCompartidos = NULL;

enum states { ST_ARRANQUE, ST_PUERTA_CERRADA, ST_ABRIENDO_PUERTA, ST_PUERTA_ABIERTA, ST_CERRANDO_PUERTA } current_state = ST_ARRANQUE;
String states_s[] = {"ST_ARRANQUE", "ST_PUERTA_CERRADA", "ST_ABRIENDO_PUERTA", "ST_PUERTA_ABIERTA", "ST_CERRANDO_PUERTA"};

enum events {
  EV_CONT,
  EV_CONTRASENIA_INVALIDA,
  EV_CONTRASENIA_VALIDA,
  EV_TOUCH_DETECTADO,
  EV_FIN_CARRERA_ABIERTO,
  EV_FIN_CARRERA_CERRADO,
  EV_INGRESO_TECLA,
  EV_TIMEOUT_PUERTA,
  EV_TIMEOUT_ERROR,
  EV_TIMEOUT_TECLADO,
  EV_CANCELAR_TECLADO,
  EV_UNKNOW
};
String events_s[] = {
  "EV_CONT", "EV_CONTRASENIA_INVALIDA", "EV_CONTRASENIA_VALIDA", "EV_TOUCH_DETECTADO",
  "EV_FIN_CARRERA_ABIERTO", "EV_FIN_CARRERA_CERRADO", "EV_INGRESO_TECLA", "EV_TIMEOUT_PUERTA",
  "EV_TIMEOUT_ERROR", "EV_TIMEOUT_TECLADO", "EV_CANCELAR_TECLADO", "EV_UNKNOW"
};

#define MAX_STATES 5
#define MAX_EVENTS 11
#define MAX_TIPO_EVENTOS 6   

typedef void (*transition)();

// Transiciones
void init_sist();
void none();
void error();
void contrasenia_invalida();
void limpiar_error();
void abrir_puerta();
void cerrar_puerta();
void ingreso_tecla();
void resetear_teclado();
void fin_carrera_abierto();
void fin_carrera_cerrado();
void mostrar_timer();
void callbackTimeoutTeclado(TimerHandle_t xTimer);

// Verificadores de eventos
events verificarTimeoutKeypad();
events verificarTimeoutPuerta();
events verificarTimeoutError();
events verificarEstadoSensorKeypad();
events verificarFinCarrera();
events verificarEstadoSensorTouch();

// Cuando sumes el touch: agregá verificarEstadoSensorTouch acá y subí MAX_TIPO_EVENTOS
events (*verificar_sensor[MAX_TIPO_EVENTOS])() = {
  verificarTimeoutKeypad, verificarTimeoutPuerta, verificarTimeoutError,
  verificarEstadoSensorTouch, verificarEstadoSensorKeypad, verificarFinCarrera
};

// ------------------------------------------------------------
// Matriz completa (igual a la original)
// EV_TOUCH_DETECTADO no se dispara todavía: falta el verificador del touch
// ------------------------------------------------------------
transition state_table[MAX_STATES][MAX_EVENTS] =
{
  //EV_CONT      , EV_CONTRASENIA_INVALIDA, EV_CONTRASENIA_VALIDA, EV_TOUCH_DETECTADO, EV_FIN_CARRERA_ABIERTO, EV_FIN_CARRERA_CERRADO, EV_INGRESO_TECLA, EV_TIMEOUT_PUERTA, EV_TIMEOUT_ERROR, EV_TIMEOUT_TECLADO, EV_CANCELAR_TECLADO
  { init_sist    , error                  , error                , error             , error                 , error                 , error          , error            , error          , error             , error            }, // ST_ARRANQUE
  { none         , contrasenia_invalida   , abrir_puerta         , abrir_puerta      , error                 , error                 , ingreso_tecla  , error            , limpiar_error  , resetear_teclado  , resetear_teclado }, // ST_PUERTA_CERRADA
  { none         , error                  , error                , error             , fin_carrera_abierto   , error                 , error          , error            , error          , error             , error            }, // ST_ABRIENDO_PUERTA
  { mostrar_timer, error                  , error                , cerrar_puerta     , error                 , error                 , error          , cerrar_puerta    , error          , error             , error            }, // ST_PUERTA_ABIERTA
  { none         , error                  , error                , error             , error                 , fin_carrera_cerrado   , error          , error            , error          , error             , error            }  // ST_CERRANDO_PUERTA
};

#define SIZE_QUEUE_TIMER 30
TaskHandle_t loopTaskHandler = NULL;
TaskHandle_t loopNewEventHandler = NULL;
QueueHandle_t queueEvents = NULL;

// ============================================================
// MQTT_conecction.h
// ============================================================
void actualizarYPublicarOcupacion(int delta) {                // delta = +1 al entrar, -1 al salir
  cantidadPersonas += delta;                                  // Suma o resta según lo que le pasaron
 
  if (cantidadPersonas < 0) cantidadPersonas = 0;             // Guarda contra números negativos (por si algo se desincroniza)
  if (cantidadPersonas > CAPACIDAD_MAXIMA) cantidadPersonas = CAPACIDAD_MAXIMA;  // Guarda contra pasarse del máximo
 
  char payload[8];                                            // Buffer chico para el número como texto
  sprintf(payload, "%d", cantidadPersonas);                   // Convierte el int a texto plano, ej. "15"
  mqttClient.publish(TOPIC_OCUPACION, payload);               // Publica el valor nuevo — texto plano, como ya definimos con la app
}

// ============================================================
// Sensors.h
// ============================================================
events verificarEstadoSensorKeypad()
{
  events new_event = EV_CONT;
  char tecla = keypad.getKey();   // devuelve la tecla solo al presionarla; NO_KEY (0) si no hay

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
    touch_mantenido = true;          // flanco: recién empieza el toque
    new_event = EV_TOUCH_DETECTADO;
  }
  else if (!tocado && touch_mantenido)
  {
    touch_mantenido = false;         // soltó: se rearma para el próximo toque
  }
  return new_event;
}

// ============================================================
// Timers.h
// ============================================================

// Callback del xTimer: corre en la tarea "Timer Service" de FreeRTOS.
// Antes levantaba un flag volatile leído sin sincronización desde otra
// tarea; ahora libera un semáforo binario, que es la forma correcta de
// pasar este tipo de señal entre tareas.
void callbackTimeoutTeclado(TimerHandle_t xTimer)
{
  xSemaphoreGive(semTimeoutTeclado);
}

events verificarTimeoutKeypad()
{
  events new_event = EV_CONT;
  // Toma no bloqueante (timeout 0): si el semáforo no fue liberado, sigue de largo.
  if (xSemaphoreTake(semTimeoutTeclado, 0) == pdTRUE)
  {
    new_event = EV_TIMEOUT_TECLADO;
  }
  return new_event;
}

events verificarTimeoutPuerta()
{
  events new_event = EV_CONT;

  // lct y timer_puerta_activo las escribe v_loop_task; se protegen con mutex
  // para leerlas de forma consistente desde esta tarea.
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

  // mostrando_error y error_timer las escribe v_loop_task; mismo mutex que arriba.
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

// ============================================================
// Actuators.h (display / buffer del teclado / timer del teclado)
// ============================================================
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
  // Vacía el semáforo por si había quedado un timeout pendiente sin consumir
  // (equivalente a lo que antes hacía "flag_timeout_teclado = false").
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
// ============================================================
// FSM_Transition.h
// ============================================================
void init_sist()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(50);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_PULSADOR, INPUT_PULLUP);
  pinMode(RED_PULSADOR, INPUT_PULLUP);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  motor_detener();

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  lcd.init();
  lcd.backlight();

  clearPasskeypad();
  mostrar_pantalla_reposo();

  timer_puerta_activo = false;
  mostrando_error = false;

  timerInactividadTeclado = xTimerCreate(
    "TimeoutTeclado",
    pdMS_TO_TICKS(TIMEOUT_INGRESO),
    pdFALSE,                       // one-shot
    NULL,
    callbackTimeoutTeclado
  );

  current_state = ST_PUERTA_CERRADA;
}

void none()  { }
void error() { }

void contrasenia_invalida()
{
  detener_timer_teclado();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DNI Incorrecto");
  digitalWrite(RED_LED, HIGH);     // LED ROJO: error
  tone(BUZZER_PIN, 500, 400);      // BUZZER: beep de error

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = true;
  error_timer = millis();
  xSemaphoreGive(mutexTimersCompartidos);

  clearPasskeypad();
}


void limpiar_error()               // también limpia el mensaje "Puerta Cerrada"
{
  digitalWrite(RED_LED, LOW);      // LED ROJO: apagado

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = false;
  xSemaphoreGive(mutexTimersCompartidos);

  mostrar_pantalla_reposo();
}

void ingreso_tecla()
{
  bool ocupadoPorError;
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  ocupadoPorError = mostrando_error;
  xSemaphoreGive(mutexTimersCompartidos);

  if (ocupadoPorError) return;     // ignora teclas mientras hay un mensaje en pantalla

  if (idxPassKeypad < TAM_DNI)
  {
    passKeypad[idxPassKeypad] = ultimo_caracter;
    idxPassKeypad++;
    passKeypad[idxPassKeypad] = '\0';
    lcd.setCursor(0, 1);
    lcd.print(passKeypad);

    xTimerReset(timerInactividadTeclado, 0);   // arranca o reinicia el conteo
  }
}

void resetear_teclado()
{
  detener_timer_teclado();
  clearPasskeypad();
  mostrar_pantalla_reposo();
}

void abrir_puerta()
{
  detener_timer_teclado();

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = false;
  xSemaphoreGive(mutexTimersCompartidos);

  digitalWrite(RED_LED, LOW);      // LED ROJO: apagado si venía de un error
  digitalWrite(GREEN_LED, HIGH);   // LED VERDE: abriendo
  lcd.clear();
  lcd.print("Abriendo...");
  motor_abrir();
  current_state = ST_ABRIENDO_PUERTA;
}

void fin_carrera_abierto()
{
  motor_detener();                 // MOTOR: llegó al final de carrera
  digitalWrite(GREEN_LED, LOW); 
  clearPasskeypad();

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  lct = millis();
  timer_puerta_activo = true;
  xSemaphoreGive(mutexTimersCompartidos);

  tiempoRestante = TIEMPO_PUERTA_ABIERTA / 1000;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tiempo restante:");
  lcd.setCursor(0, 1);
  lcd.print(tiempoRestante);

  current_state = ST_PUERTA_ABIERTA;
}

void mostrar_timer()
{
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  unsigned long inicio = lct;
  xSemaphoreGive(mutexTimersCompartidos);

  int nuevoTiempo = (TIEMPO_PUERTA_ABIERTA / 1000) - (int)((millis() - inicio) / 1000);

  if (nuevoTiempo != tiempoRestante && nuevoTiempo >= 0)
  {
    tiempoRestante = nuevoTiempo;
    lcd.setCursor(0, 1);
    lcd.print(tiempoRestante);
    lcd.print("   ");

    if (tiempoRestante <= 5 && tiempoRestante > 0)
    {
      tone(BUZZER_PIN, 1500, 50);  // BUZZER: beep en la cuenta regresiva
    }
  }
}

void cerrar_puerta()
{
  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  timer_puerta_activo = false;
  xSemaphoreGive(mutexTimersCompartidos);

  lcd.clear();
  lcd.print("Cerrando...");
  motor_cerrar();                  // MOTOR: gira en sentido de cierre
  current_state = ST_CERRANDO_PUERTA;
}

void fin_carrera_cerrado()
{
  motor_detener();                 // MOTOR: llegó al final de carrera
  clearPasskeypad();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Puerta Cerrada");

  xSemaphoreTake(mutexTimersCompartidos, portMAX_DELAY);
  mostrando_error = true;
  error_timer = millis();
  xSemaphoreGive(mutexTimersCompartidos);

  current_state = ST_PUERTA_CERRADA;
}

// ============================================================
// esp32-blink.ino
// ============================================================
void v_task_get_new_event(void *pvParameters)
{
  enum events new_event;
  short indice = 0;
  
  while (1)
  {
    indice = (indice + 1) % MAX_TIPO_EVENTOS;

    new_event = verificar_sensor[indice]();

    if (xQueueSend(queueEvents, &new_event, pdMS_TO_TICKS(200)) != pdPASS)
    {
      Serial.println("Get_event TIMEOUT: Queue is full.");
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void maquina_de_estados()
{
  enum events new_event;

  if (xQueueReceive(queueEvents, &new_event, portMAX_DELAY) == pdTRUE)
  {
    if ((int)new_event < MAX_EVENTS && (int)current_state < MAX_STATES)
    {
      if (new_event != EV_CONT)
      {
        DebugPrintEstado(states_s[current_state], events_s[new_event]);
      }
      state_table[current_state][new_event]();
    }
  }
}

void v_loop_task(void *pvParameters)
{
  while (1)
  {
    maquina_de_estados();
  }
}

void loop() { }

void setup()
{
  Serial.begin(115200);
  queueEvents = xQueueCreate(SIZE_QUEUE_TIMER, sizeof(events));

  // Mutex y semáforo se crean ANTES de lanzar las tareas, así ninguna de
  // las dos puede llegar a usarlos sin estar inicializados.
  mutexTimersCompartidos = xSemaphoreCreateMutex();
  semTimeoutTeclado = xSemaphoreCreateBinary();

  xTaskCreate(v_loop_task,          "v_loop_task",          4096, NULL, 1, &loopNewEventHandler);
  xTaskCreate(v_task_get_new_event, "v_task_get_new_event", 4096, NULL, 1, &loopTaskHandler);
}