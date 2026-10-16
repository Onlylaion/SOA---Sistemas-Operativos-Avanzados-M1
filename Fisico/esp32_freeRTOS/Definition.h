#ifndef PUERTA_DEFINITION_H
#define PUERTA_DEFINITION_H

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <PubSubClient.h>

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

// ---- WiFi ----
const char* WIFI_SSID = "TeleCentro-85b5";
const char* WIFI_PASS = "TKHZNZUMNJRZ";

// ---- MQTT ----
const char* MQTT_BROKER    = "192.168.0.7";       // IP del broker MQTT
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_CLIENT_ID = "esp32-puerta-gym";
#define TOPIC_COMANDO "esp32/myMQTT/comando"        // Topic donde se recibe el comando
#define TOPIC_DATOS "esp32/myMQTT/sensor"            // Topic donde se publican los datos del sensor

WiFiClient espClient;
PubSubClient mqttClient(espClient);

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
#define UMBRAL_TOUCH 1000        
bool touch_mantenido = false;    // evita disparar el evento en cada lectura mientras siga tocado

// ---- Timeout de teclado - xTimer y semáforo binario ----
TimerHandle_t     timerInactividadTeclado = NULL;
SemaphoreHandle_t semTimeoutTeclado = NULL;

// ---- Timeout de puerta - millis() ----
bool timer_puerta_activo = false;
unsigned long lct = 0;
int tiempoRestante = TIEMPO_PUERTA_ABIERTA / 1000;

// ---- Mensaje temporal en pantalla (error / puerta cerrada) ----
bool mostrando_error = false;
unsigned long error_timer = 0;

// Mutex que protege lct/timer_puerta_activo/error_timer/mostrando_error,
// compartido entre v_loop_task (escribe) y v_task_get_new_event (lee).
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
  EV_COMANDO_MQTT,
  EV_UNKNOW
};
String events_s[] = {
  "EV_CONT", "EV_CONTRASENIA_INVALIDA", "EV_CONTRASENIA_VALIDA", "EV_TOUCH_DETECTADO",
  "EV_FIN_CARRERA_ABIERTO", "EV_FIN_CARRERA_CERRADO", "EV_INGRESO_TECLA", "EV_TIMEOUT_PUERTA",
  "EV_TIMEOUT_ERROR", "EV_TIMEOUT_TECLADO", "EV_CANCELAR_TECLADO", "EV_COMANDO_MQTT", "EV_UNKNOW"
};

#define MAX_STATES 5
#define MAX_EVENTS 12
#define MAX_TIPO_EVENTOS 6

typedef void (*transition)();

// Transiciones
void init_sist();
void none();
void error();
void contrasenia_invalida();
void limpiar_error();
void abrir_puerta();
void abrir_puerta_ingreso();
void abrir_puerta_salida();
void cerrar_puerta();
void ingreso_tecla();
void resetear_teclado();
void fin_carrera_abierto();
void fin_carrera_cerrado();
void mostrar_timer();
void callbackTimeoutTeclado(TimerHandle_t xTimer);
void obtener_datos_teclado();
void obtener_valor_touch();

// Verificadores de eventos
events verificarTimeoutKeypad();
events verificarTimeoutPuerta();
events verificarTimeoutError();
events verificarEstadoSensorKeypad();
events verificarFinCarrera();
events verificarEstadoSensorTouch();

events (*verificar_sensor[MAX_TIPO_EVENTOS])() = {
  verificarTimeoutKeypad, verificarTimeoutPuerta, verificarTimeoutError,
  verificarEstadoSensorTouch, verificarEstadoSensorKeypad, verificarFinCarrera
};

// ------------------------------------------------------------
// Matriz de estados
// ------------------------------------------------------------
transition state_table[MAX_STATES][MAX_EVENTS] =
{
  //EV_CONT      , EV_CONTRASENIA_INVALIDA, EV_CONTRASENIA_VALIDA , EV_TOUCH_DETECTADO , EV_FIN_CARRERA_ABIERTO, EV_FIN_CARRERA_CERRADO, EV_INGRESO_TECLA, EV_TIMEOUT_PUERTA, EV_TIMEOUT_ERROR, EV_TIMEOUT_TECLADO, EV_CANCELAR_TECLADO, EV_COMANDO_MQTT
  { init_sist    , error                  , error                 , error              , error                 , error                 , error          , error            , error          , error             , error            , error                  }, // ST_ARRANQUE
  { none         , contrasenia_invalida   , abrir_puerta_ingreso  , abrir_puerta_salida, error                 , error                 , ingreso_tecla  , error            , limpiar_error  , resetear_teclado  , resetear_teclado , obtener_datos_teclado  }, // ST_PUERTA_CERRADA
  { none         , error                  , error                 , error              , fin_carrera_abierto   , error                 , error          , error            , error          , error             , error            , obtener_valor_touch    }, // ST_ABRIENDO_PUERTA
  { mostrar_timer, error                  , error                 , cerrar_puerta      , error                 , error                 , error          , cerrar_puerta    , error          , error             , error            , obtener_valor_touch    }, // ST_PUERTA_ABIERTA
  { none         , error                  , error                 , error              , error                 , fin_carrera_cerrado   , error          , error            , error          , error             , error            , error                  }, // ST_CERRANDO_PUERTA
};

#define SIZE_QUEUE_TIMER 30
TaskHandle_t loopTaskHandler = NULL;
TaskHandle_t loopNewEventHandler = NULL;
QueueHandle_t queueEvents = NULL;

#define INTERVALO_RECONEXION_MQTT 5000   
#define SIZE_QUEUE_DATOS 1
QueueHandle_t queueDatos = NULL;     
TaskHandle_t  mqttTaskHandler = NULL;

// Para MQTT

// Mutex que proteje el acceso al array passKeypad y al índice idxPassKeypad.
// Se utiliza para mandar mensajes por mqtt
SemaphoreHandle_t mutexUltimaTecla = NULL;

// Tipos de datos posibles
enum TipoMensaje_t {
  TIPO_ENTERO,
  TIPO_CHAR
};

// Estructura del mensaje para la cola
struct MensajeQueue_t {
  TipoMensaje_t tipo;
  int valor_touch;
  char passKeypad_mqtt[TAM_DNI + 1];
};


#endif // PUERTA_DEFINITION_H
