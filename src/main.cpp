#include "secrets.h"
#include <WiFi.h>
#include <PubSubClient.h>

#define Water_level_sensor 32
#define Flow_sensor 33
#define Relay_pin_1 2
#define Relay_pin_2 4
#define boton_pin 15

// Umbral de nivel de agua
const int umbral_bajo = 30;
const int umbral_alto = 40;

// Variables para el motor
bool motorEncendido = false;
bool cambioLocal = false;

// Variables para el sensor de flujo
volatile int pulseCount = 0;
unsigned long lastNivelMillis = 0;
unsigned long lastFlowMillis = 0;

// Estado del sistema
enum EstadoSistema { APAGADO, ENCENDIDO };
EstadoSistema estadoSistema = APAGADO;
bool ultimoEstadoBoton = false;

unsigned long lastDebounceTime = 0;  // Para el debounce del botón
bool buttonPressed = false;          // Evita múltiples publicaciones MQTT

// Variables para el cambio de estado del boton
unsigned long tiempoUltimoCambio = 0;
const unsigned long debounceDelay = 50;

// Variables para el sensor de flujo de agua
float flowRate = 0;

const char* ssidList[] = {
	SSID_1,
	SSID_2,
	SSID_3,
	SSID_4
};

const char* passwordList[] = {
	PASSWORD_1,
	PASSWORD_2,
	PASSWORD_3,
	PASSWORD_4
};

const int numRedes = sizeof(ssidList) / sizeof(ssidList[0]);

WiFiClient espClient;

const char* mqttUser = MQTT_USERNAME;
const char* mqttPassword = MQTT_PASSWORD;
const char* mqtt_server = MQTT_BROKER_IP;
const int mqttPort = MQTT_PORT;

PubSubClient client(espClient);

void conectarWiFi() {
	for (int i = 0; i < numRedes; i++) {
		WiFi.begin(ssidList[i], passwordList[i]);
		Serial.print("Intentando conectar a: ");
		Serial.println(ssidList[i]);

		int tiempoEspera = 0;
		while (WiFi.status() != WL_CONNECTED && tiempoEspera < 10) {
			delay(1000);
			Serial.print(".");
			tiempoEspera++;
		}

		if (WiFi.status() == WL_CONNECTED) {
			Serial.println("\n✅ Conectado a red WiFi");
			return;
		}
	}
	Serial.println("\n💔 No se pudo conectar a ninguna red. Reintentando en 10s...");
	delay(2500);
	conectarWiFi();
}

void reconnectMQTT() {
	while (!client.connected()) {
		Serial.println("Intentando conexi\xC3\xB3n MQTT...");
		if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
			Serial.println("✅ Conectado al broker MQTT");
			client.subscribe(MQTT_TOPIC);
		} else {
			Serial.println("❌ Error, rc=");
			Serial.print(client.state());
			delay(2500);
		}
	}
}

void IRAM_ATTR pulseCounter() {
	pulseCount++;
}

void callback(char* topic, byte* payload, unsigned int length) {
	String mensaje;
	for (int i = 0; i < length; i++) mensaje += (char)payload[i];

	if (String(topic) == MQTT_TOPIC) {
		if (cambioLocal) {
			cambioLocal = false;
			return; // Evita que el callback overescriba el estado cambiado por el botón
		}
		estadoSistema = (mensaje == "true") ? ENCENDIDO : APAGADO;
		Serial.println(estadoSistema == ENCENDIDO ? "🔥 Encendido (MQTT)" : "❄ Apagado (MQTT)");
	}
}


void manejarEstado() {
	if (estadoSistema == ENCENDIDO) {
		unsigned long currentMillis = millis();
		if (currentMillis - lastNivelMillis >= 500) {
			lastNivelMillis = currentMillis;
			int valor = analogRead(Water_level_sensor);
			int porcentaje = constrain(map(valor, 0, 4095, 0, 100), 0, 100);
			client.publish(MQTT_TOPIC_2, String(porcentaje).c_str());
			Serial.println("Nivel de agua: " + String(porcentaje) + "%");

			if (porcentaje < umbral_bajo && !motorEncendido) {
				digitalWrite(Relay_pin_1, HIGH);
				digitalWrite(Relay_pin_2, HIGH);
				motorEncendido = true;
			} else if (porcentaje >= umbral_alto && motorEncendido) {
				digitalWrite(Relay_pin_1, LOW);
				digitalWrite(Relay_pin_2, LOW);
				motorEncendido = false;
			}
		}
		if (currentMillis - lastFlowMillis >= 1000) {
			lastFlowMillis = currentMillis;
			detachInterrupt(digitalPinToInterrupt(Flow_sensor));
			flowRate = pulseCount / 7.5;
			pulseCount = 0;
			attachInterrupt(digitalPinToInterrupt(Flow_sensor), pulseCounter, FALLING);
			client.publish(MQTT_TOPIC_3, String(flowRate).c_str());
			Serial.println("Flujo de agua: " + String(flowRate) + " L/min");
		}
	} else {
		if (motorEncendido) {
			digitalWrite(Relay_pin_1, LOW);
			digitalWrite(Relay_pin_2, LOW);
			motorEncendido = false;
			Serial.println("❄ Motores apagados (Sistema APAGADO)");
		}
	}
}

void leerBoton();

void setup() {
	Serial.begin(115200);
	analogReadResolution(12);

	pinMode(Relay_pin_1, OUTPUT);
	pinMode(Relay_pin_2, OUTPUT);
	pinMode(Flow_sensor, INPUT_PULLUP);
	pinMode(boton_pin, INPUT_PULLUP);
	digitalWrite(Relay_pin_1, LOW);
	digitalWrite(Relay_pin_2, LOW);

	attachInterrupt(digitalPinToInterrupt(Flow_sensor), pulseCounter, FALLING);

	conectarWiFi();
	client.setServer(mqtt_server, mqttPort);
	reconnectMQTT();
	client.setCallback(callback);
}

void loop() {
	if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();
	leerBoton();
  	manejarEstado();
}

void leerBoton() {
	int lectura = digitalRead(boton_pin);
	if (lectura == HIGH && !ultimoEstadoBoton) {
		ultimoEstadoBoton = true;
		estadoSistema = (estadoSistema == ENCENDIDO) ? APAGADO : ENCENDIDO; // Cambia entre ENCENDIDO y APAGADO
		Serial.println(estadoSistema == ENCENDIDO ? "🔥 Encendido (Botón)" : "❄ Apagado (Botón)");
		cambioLocal = true; // evita que el callback lo sobreescriba
		client.publish(MQTT_TOPIC, (estadoSistema == ENCENDIDO) ? "true" : "false");
	} else if (lectura == LOW) {
		ultimoEstadoBoton = false;
	}
}