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
#include <freertos/queue.h>
#include <SD.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define LOAD_CELL_DOUT GPIO_NUM_32
#define LOAD_CELL_SCK  GPIO_NUM_33
#define CS GPIO_NUM_5           /* chip select do módulo SD card = 5: CS, 18: CLK, 19: MISO, 23: MOSI */
#define QUEUE_SIZE 512

typedef struct {
	uint16_t timeout = 10000;
	float scale = -268.20;
	float prop_mass = 1000;
	float weight = 217.0;
}config_t;

typedef struct {
	float value;
	uint16_t time;
}sample_data_t;

static const char *TAG_SD = "SD";
static const char *TAG_HX711 = "HX711";
static const char *TAG_FS = "LittleFS";
static const char *TAG_WIFI = "WiFi";
static const char *TAG_WS = "WEBSOCKET";
static const char *TAG_SAMPLE = "SAMPLE";
static const char *TAG_RTOS = "RTOS";

config_t cfg;
config_t *pCfg = &cfg;
HX711 loadCell;
String hour = "";
String date = "";
File cfg_file;
File log_file;
const char* ssid = "ESP32_AP";
const char* password = "decola_asa";
const float gravity = 9.789;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
QueueHandle_t dataQueue;
TaskHandle_t readingTaskHandle = NULL;
TaskHandle_t webSocketSendTaskHandle = NULL;
bool startReading = false;
bool sdStatus = false;

/* Protótipo de Funções */
void calibrate(float weight);
bool initSDcard(void);
void checkSDconfig(config_t* cfg);
String readLine(void);
void initLoadCell(void);
void initLittleFS(void);
void initWifi(void);
void initWebSocket(void);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, void *arg, uint8_t *data, size_t len);
void readingTask(void *pvParameters);
void webSocketSendTask(void *pvParameters);

/* ============================================================================================================================= */
void setup() {
	gpio_set_direction(CS, GPIO_MODE_OUTPUT);
	gpio_set_level(CS, 0);
	dataQueue = xQueueCreate(QUEUE_SIZE, sizeof(sample_data_t));
  	if (dataQueue == NULL) {
    	ESP_LOGE(TAG_RTOS, "Erro ao criar a fila\n");
  	}
	initLittleFS();
	checkSDconfig(pCfg);
	initWifi();
	initWebSocket();
	initLoadCell();
	xTaskCreatePinnedToCore(readingTask, "Reading Task", 8192, NULL, 2, NULL, 0);
  	xTaskCreatePinnedToCore(webSocketSendTask, "WebSocket Send Task", 8192, NULL, 1, NULL, 0);
	
}

void loop() {
	ws.cleanupClients();
}
/* ============================================================================================================================= */
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
	if (!SD.begin(CS)) {
    	ESP_LOGE(TAG_SD, "Falha na inicializacao.\n");
    	return false;
	}
	else{
		ESP_LOGI(TAG_SD, "Cartao SD inicializado.\n");
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
		sdStatus = true;
		String temp = "";
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
		sdStatus = false;
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
			startReading = true;
		}
		else if(type == "sd_status"){
			StaticJsonDocument<96> json;
			String message = "";
			json["type"] = "sd_status";
			json["data"] = String(sdStatus);
			serializeJson(json, message);
      		ws.textAll(message);
		}
		else if(type == "sample_running"){
			StaticJsonDocument<96> json;
			String message = "";
			json["type"] = "sample_running";
			json["data"] = String(startReading);
			serializeJson(json, message);
      		ws.textAll(message);
		}
	}
}

void readingTask(void *pvParameters) {
	TickType_t time_interval = 0;
	sample_data_t reading;
	String filename = "";

  	while (1) {
    	if (startReading) {
			filename = "/" + date + hour + ".csv";
			ESP_LOGI(TAG_SAMPLE, "Amostragem...\n");
			/* Abre o arquivo no SD */
			ESP_LOGI(TAG_SAMPLE, "Full filename %s\n", filename);
			log_file = SD.open(filename, FILE_WRITE);
			if (log_file)
				ESP_LOGI(TAG_SAMPLE, "Arquivo %s criado com sucesso.\n", filename);
			else
				ESP_LOGE(TAG_SAMPLE, "Erro ao criar arquivo.\n");
			log_file.println("reading,time");

			/* Configura o timeout de 10 segundos */
			TickType_t startTime = xTaskGetTickCount();
			const TickType_t timeout = pdMS_TO_TICKS(pCfg->timeout);
		
			/* Continua a leitura enquanto startReading for true e dentro do timeout */
			while (startReading && (xTaskGetTickCount() - startTime) < timeout) {
				reading.value = loadCell.get_units(1) * gravity;

				if (reading.value != 0.0) {
					time_interval = xTaskGetTickCount() - startTime;
					reading.time = (uint16_t)time_interval;
					ESP_LOGI(TAG_SAMPLE, "Time: %d ms, Reading: %.3f\n", reading.time, reading.value);
					/* salva a leitura no cartao sd */
					if (log_file) {
						log_file.print(reading.value);
						log_file.print(",");
						log_file.println(reading.time);
					}
					/* Envia para a fila */
					if (xQueueSend(dataQueue, &reading, portMAX_DELAY) != pdPASS) {
						ESP_LOGE(TAG_RTOS, "Erro ao enviar para a fila.\n");
					}
					vTaskDelay(pdMS_TO_TICKS(15U));
				}
				else{
					/* Aguarda 15ms */
					vTaskDelay(pdMS_TO_TICKS(15U));
				}
			}
			/* Fecha o arquivo quando startReading for false ou timeout */
			log_file.close();
			startReading = false; /* Reseta a variável startReading após o timeout */
			ESP_LOGI(TAG_SAMPLE, "Amostragem finalizada.\n");
		}
		else {
			/* Aguarda até que startReading seja true */
			vTaskDelay(pdMS_TO_TICKS(100U));
		}
	}
}

void webSocketSendTask(void *pvParameters) {
	sample_data_t sample;
	StaticJsonDocument<96> json;
	String message = "";
  	while (1) {
		ESP_LOGI(TAG_WS, "ws Task enter.\n");
    	/* Verifica se há dados na fila */
    	if (xQueueReceive(dataQueue, &sample, portMAX_DELAY) == pdPASS) {
			ESP_LOGI(TAG_WS, "Dado lido da fila\n");
			json["type"] = "sample";
			JsonObject data = json.createNestedObject("data");
			data["value"] = String(sample.value);
			data["time"] = String(sample.time);
			serializeJson(json, message);
      		
      		ws.textAll(message); /* esta resetando a cpu aqui */
			ESP_LOGI(TAG_WS, "msg enviada para ws.\n");
    	}
    	vTaskDelay(pdMS_TO_TICKS(60U));
  	}
}