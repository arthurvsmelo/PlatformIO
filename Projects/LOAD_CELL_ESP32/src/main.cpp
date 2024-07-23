#include <Arduino.h>
#include <HX711.h>
#include <esp32-hal-gpio.h>
#include <esp32-hal-timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define LOAD_CELL_DOUT GPIO_NUM_32
#define LOAD_CELL_SCK  GPIO_NUM_33
#define CS GPIO_NUM_5           /* chip select do módulo SD card */
/* 5: CS, 18: CLK, 19: MISO, 23: MOSI */

typedef struct {
	uint16_t timeout = 10000;
	float scale = -268.20;
	float prop_mass = 1000;
	float weight = 217.0;
}config_t;

config_t cfg;
config_t *pCfg = &cfg;
HX711 loadCell;
const float gravity = 9.789;
String hour;
String date;
File cfg_file;
File log_file;
volatile uint16_t timer_count = 0;
bool start_sampling;
/* Usuário e senha da rede local wifi */
const char* ssid = "ESP32_AP";
const char* password = "decola_asa";
/* Inicia servidor assíncrono */
AsyncWebServer server(80);
/* Inicia comunicação via websocket */
AsyncWebSocket ws("/ws");

void IRAM_ATTR Timer0_ISR()
{
	if(timer_count >= 65535)
		timer_count = 0;
	else
    	timer_count++;
}

/* Protótipo de Funções */
void calibrate(float weight);
bool initSDcard(void);
void checkSDconfig(config_t* cfg);
String readLine(void);
void initLoadCell(void);
bool initLittleFS(void);
void initWifi(void);
void initWebSocket(void);
void sample(config_t* cfg);
void notifyClients(String message);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, void *arg, uint8_t *data, size_t len);
void setTimer(void);

void setup() {
	gpio_set_direction(CS, GPIO_MODE_OUTPUT);
	Serial.begin(115200);
	initLittleFS();
	initWifi();
	initWebSocket();
	initLoadCell();
	initSDcard();
	checkSDconfig(pCfg);	
}

void loop() {
	ws.cleanupClients();
	if(start_sampling){
		sample(&cfg);
	}
	vTaskDelay(10/portTICK_PERIOD_MS);
}

/* Funções */

/**
 * @brief Função de calibração da célula de carga
 * @param weight Peso conhecido a ser posicionado na célula
 */
void calibrate(float weight){
	short n = 5;
	float cal = 0.0;
	loadCell.set_scale();
	loadCell.tare();
	Serial.println("Ponha um peso conhecido na celula");
	while(n--){
		Serial.print(n);
		Serial.println(" ...");
		vTaskDelay(1000);
	}
	cal = loadCell.get_units(10);
	cfg.scale = cal / weight;
	loadCell.set_scale(cfg.scale);
	Serial.print("Nova escala: ");
	Serial.println(cfg.scale);
	Serial.println("Calibrado!");
}
/**
 * @brief Inicializa o cartão SD. 
 * 
 */
bool initSDcard(void){
	String json_output;
	StaticJsonDocument<128> doc;
	doc["type"] = "sd_status";
	if (!SD.begin(CS)) {
    	Serial.println("Falha na inicializacao.");
		doc["data"] = "ERRO";
		serializeJson(doc, json_output);
		notifyClients(json_output);
    	return false;
	}
	else{
		Serial.println("Cartao SD inicializado!");
		doc["data"] = "OK";
		serializeJson(doc, json_output);
		notifyClients(json_output);
	}
	return true;
}
/**
 * @brief Verifica se há um arquivo de configuração. Se existir, lê o arquivo
 * e aplica as configurações salva ao programa. Caso não exista, cria um novo
 * arquivo com as configurações padrão.
 */
void checkSDconfig(config_t* pcfg){
	if(initSDcard()){
		String temp;
		/* checa se existe arquivo de configuração */
		if(SD.exists("/config.txt")){
			cfg_file = SD.open("/config.txt");
			if(cfg_file){
				/* escreve as configurações padrão */
				/* lê e define o fator de calibração */
				temp = readLine();
				pcfg->scale = temp.toFloat();
				/* lê e define o timeout da amostragem */
				temp = readLine();
				pcfg->timeout = temp.toInt();
				temp = readLine();
				pcfg->weight = temp.toFloat();
				Serial.println("Configuracoes feitas!");
				cfg_file.close();
			}
			else{
				Serial.println("Erro ao abrir arquivo de configuracao!");
			}
		}
		else{
			/* se não existir arquivo de cfg, cria um novo */
			cfg_file = SD.open("/config.txt", FILE_WRITE);
			if(cfg_file){
				/* escreve os valores padrão salvos hardcoded nas variaveis globais */
				cfg_file.println((String)pcfg->scale);
				cfg_file.println((String)pcfg->timeout);
				cfg_file.println((String)pcfg->weight);
				Serial.println("Arquivo de configuracao criado com os valores padrão.");
				cfg_file.close();
			}
			else{
				Serial.println("Erro ao criar arquivo de configuracao!");
			}
		}
	}
	else{
		Serial.println("Falha na configuracao.");
		return;
	}		
}

/**
 * @brief Funcao para ler uma linha do arquivo de configuração
 * @return String contendo a leitura de uma linha do arquivo
 */
String readLine(){
	char c;
	String line = "";
	while(true){
		c = cfg_file.read();
    	line = line + c;
    	if(c == '\n'){
      		return line;
    	}
	}
}
/**
 * @brief Inicializa a célula de carga.
 */
void initLoadCell(void){
	loadCell.begin(LOAD_CELL_DOUT, LOAD_CELL_SCK, 128);
	loadCell.set_scale(cfg.scale);
	loadCell.tare();
	Serial.println("Celula de carga iniciada.");
}

/**
 * @brief Inicia a WiFi como ponto de acesso.
 */
void initWifi(void){
	WiFi.softAP(ssid, password);
	IPAddress IP = WiFi.softAPIP();
	Serial.print("IP do ponto de acesso: ");
	Serial.println(IP);
}
/**
 * @brief Inicia o LittleFS para ler os arquivos da página HTML.
 * 
 * @return true, se obteve sucesso;
 * @return false, se houve erro
 */
bool initLittleFS(void){
	if (!LittleFS.begin(true)) {
    	Serial.println("Erro ao montar LittleFS");
    	return false;
  	}
	else{
		return true;
	}
}
/**
 * @brief Inicia o servidor websocket.
 */
void initWebSocket(void){
	ws.onEvent(onEvent);
	server.addHandler(&ws);
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
	request->send(LittleFS, "/index.html", "text/html",false);
	});
	server.serveStatic("/", LittleFS, "/");
	/* Inicia servidor */
	server.begin();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, 
	void *arg, uint8_t *data, size_t len) {
	
	switch (type) {
		case WS_EVT_CONNECT:
			Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
			break;
		case WS_EVT_DISCONNECT:
			Serial.printf("WebSocket client #%u disconnected\n", client->id());
			break;
		case WS_EVT_DATA:
			handleWebSocketMessage(arg, data, len);
			break;
		case WS_EVT_PONG:
		case WS_EVT_ERROR:
			break;
	}
}
/**
 * Recebe e trata as mensagens oriundas dos clientes.
 */
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
	
	AwsFrameInfo *info = (AwsFrameInfo*)arg;
	
	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
		data[len] = 0;
		StaticJsonDocument<128> json_data;
		deserializeJson(json_data, data);
		String type = json_data["type"];
		if(type == "sample_begin"){
			date = (const char*)json_data["data"]["date"];
			hour = (const char*)json_data["data"]["time"];
			start_sampling = true;
		}
	}
}
/**
 * Envia mensagens para todos os clientes.
 */
void notifyClients(String message) {
	ws.textAll(message);
}

void sample(config_t* cfg){
	Serial.println("Iniciando amostragem...");
	uint16_t last_timer_count_value = timer_count;
	uint16_t time = 0;
	long reading;
	String json_output;
	StaticJsonDocument<128> doc;
	doc["type"] = "sample";
	JsonObject obj = doc.createNestedObject("data");
	/* abre um arquivo de log na pasta /tests com o nome = data-hora */
	log_file = SD.open("/" + hour + ".txt", FILE_WRITE);
	log_file.println("reading,time");
	notifyClients("{'type':'status_info','data':'Running'}");
	while((timer_count - last_timer_count_value) <= cfg->timeout){
		reading = loadCell.get_units(1) * gravity;
		if(reading >= 10000.0){
			time = timer_count - last_timer_count_value;
			Serial.println(time);
			/* salva a leitura no cartao sd */
			if(log_file){
				log_file.print(reading);
				log_file.print(",");
				log_file.println(time);
			}
			/* constrói o json */
			obj["reading"] = reading;
			obj["time"] = time;
			serializeJson(doc, json_output);
			/* envia o json contendo o valor */
			notifyClients(json_output);
		}
	}
	/* fecha o arquivo de log */
	log_file.close();
	start_sampling = false;
	notifyClients("{'type':'status_info','data':'Not running'}");
	Serial.println("Amostragem finalizada.");
}

void setTimer(void){
	hw_timer_t *Timer0_Cfg = NULL;
	Timer0_Cfg = timerBegin(0, 80, true);
	timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
    timerAlarmWrite(Timer0_Cfg, 1000, true);
    timerAlarmEnable(Timer0_Cfg);
}