#include <Arduino.h>
#include <HX711.h>
#include <esp32-hal-gpio.h>
#include <esp32-hal-timer.h>

#define CORE_DEBUG_LEVEL 3
#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include <esp_err.h>
#include <esp_log.h>
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

static const char *TAG_SD = "SD";
static const char *TAG_HX711 = "HX711";
static const char *TAG_FS = "LittleFS";
static const char *TAG_WIFI = "WiFi";
static const char *TAG_WS = "WEBSOCKET";
static const char *TAG_SAMPLE = "SAMPLE";

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
void initLittleFS(void);
void initWifi(void);
void initWebSocket(void);
void sample(config_t* cfg);
void notifyClients(String message);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, void *arg, uint8_t *data, size_t len);
void setTimer(void);

void setup() {
	gpio_set_direction(CS, GPIO_MODE_OUTPUT);
	setTimer();
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
	vTaskDelay(pdMS_TO_TICKS(500));
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
	ESP_LOGI(TAG_HX711, "Ponha um peso conhecido na celula\n");
	while(n--){
		ESP_LOGI(TAG_HX711, "%i" , n);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	cal = loadCell.get_units(10);
	cfg.scale = cal / weight;
	loadCell.set_scale(cfg.scale);
	ESP_LOGI(TAG_HX711, "Nova escala: %f", cfg.scale);
	ESP_LOGI(TAG_HX711, "Calibrado!\n");
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
    	ESP_LOGE(TAG_SD, "Falha na inicializacao.\n");
		doc["data"] = "ERRO";
		serializeJson(doc, json_output);
		notifyClients(json_output);
    	return false;
	}
	else{
		ESP_LOGI(TAG_SD, "Cartao SD inicializado.\n");
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
				ESP_LOGI(TAG_SD, "Configuracoes feitas!\n");
				cfg_file.close();
			}
			else{
				ESP_LOGE(TAG_SD, "Erro ao abrir arquivo de configuracao!\n");
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
				ESP_LOGI(TAG_SD, "Arquivo de configuracao criado com os valores padrão.\n");
				cfg_file.close();
			}
			else{
				ESP_LOGE(TAG_SD, "Erro ao criar arquivo de configuracao!\n");
			}
		}
	}
	else{
		ESP_LOGE(TAG_SD, "Falha na configuracao.\n");
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
	ESP_LOGI(TAG_HX711, "Celula de carga iniciada.\n");
}

/**
 * @brief Inicia a WiFi como ponto de acesso.
 */
void initWifi(void){
	WiFi.softAP(ssid, password);
	IPAddress IP = WiFi.softAPIP();
	ESP_LOGI(TAG_WIFI, "IP do ponto de acesso: %s", IP.toString().c_str());
}
/**
 * @brief Inicia o LittleFS para ler os arquivos da página HTML.
 * 
 * @return true, se obteve sucesso;
 * @return false, se houve erro
 */
void initLittleFS(void){
	if (!LittleFS.begin(true)) {
    	ESP_LOGE(TAG_FS, "Erro ao montar LittleFS");
  	}
	else{
		ESP_LOGI(TAG_FS, "LittleFS montado.\n");
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
	ESP_LOGI(TAG_WS, "Websocket iniciado.\n");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, 
	void *arg, uint8_t *data, size_t len) {
	
	switch (type) {
		case WS_EVT_CONNECT:
			ESP_LOGI(TAG_WS, "WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
			break;
		case WS_EVT_DISCONNECT:
			ESP_LOGI(TAG_WS, "WebSocket client #%u disconnected\n", client->id());
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
	log_file = SD.open("/test.txt", FILE_WRITE);
	if(log_file)
		ESP_LOGI(TAG_SAMPLE, "Arquivo criado com sucesso.\n");
	else
		ESP_LOGE(TAG_SAMPLE, "Erro ao criar arquivo.\n");
	log_file.println("reading,time");
	notifyClients("{'type':'status_info','data':'Running'}");
	while((timer_count - last_timer_count_value) <= cfg->timeout){
		reading = loadCell.get_units(1) * gravity;
		if(reading >= 10000.0){
			time = timer_count - last_timer_count_value;
			ESP_LOGI(TAG_SAMPLE, "Time: %d ms, Reading: %ld\n", time, reading);
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
			vTaskDelay(pdMS_TO_TICKS(13));
		}
	}
	/* fecha o arquivo de log */
	log_file.close();
	start_sampling = false;
	notifyClients("{'type':'status_info','data':'Not running'}");
	ESP_LOGI(TAG_SAMPLE, "Amostragem finalizada.\n");
}

void setTimer(void){
	hw_timer_t *Timer0_Cfg = NULL;
	Timer0_Cfg = timerBegin(0, 80, true);
	timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
    timerAlarmWrite(Timer0_Cfg, 1000, true);
    timerAlarmEnable(Timer0_Cfg);
}