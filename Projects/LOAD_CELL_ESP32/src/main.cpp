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

typedef struct config{
	uint16_t timeout;
	float cal_param;
	float prop_mass;
} config_params;

HX711 loadCell;
float cal_param = -268.20;
const float gravity = 9.789;
uint16_t timeout = 10000;                /* timeout em milissegundos */
File cfg;
File log;
String timestamp;
volatile uint16_t timer_count = 0;
bool sample_isrunning;
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
void checkSDconfig(void);
bool setConfig(String new_cfg);
void SDWriteLog(void);
String readLine(void);
void initLoadCell(void);
bool initLittleFS(void);
void initWifi(void);
void initWebSocket(void);
bool sample(void);
void notifyClients(String message);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, void *arg, uint8_t *data, size_t len);
void setTimer(void);

void setup() {
	gpio_set_direction(CS, GPIO_MODE_OUTPUT);
	Serial.begin(115200);
	gpio_init();
	initLoadCell();
	initWifi();
	initWebSocket();	
}

void loop() {
	
	while(timeout--){
		
	}
	
	vTaskDelay(12);
	ws.cleanupClients();
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
	cal_param = cal / weight;
	loadCell.set_scale(cal_param);
	Serial.print("Nova escala: ");
	Serial.println(cal_param);
	Serial.println("Calibrado!");
}
/**
 * @brief Inicializa o cartão SD. 
 * 
 */
bool initSDcard(void){
	if (!SD.begin(CS)) {
    	Serial.println("Falha na inicializacao.");
    	Serial.println("Cheque se ha cartao SD inserido ou se ha falha na conexao.");
		Serial.println("Apos checar, perte o botao de reset.");
    	return false;
	}
	else{
		Serial.println("Cartao SD inicializado!");
	}
	return true;
}
/**
 * @brief Verifica se há um arquivo de configuração. Se existir, lê o arquivo
 * e aplica as configurações salva ao programa. Caso não exista, cria um novo
 * arquivo com as configurações padrão.
 */
void checkSDconfig(void){
	if(initSDcard()){
		String temp;
		/* checa se existe arquivo de configuração */
		if(SD.exists("/config.txt")){
			cfg = SD.open("/config.txt");
			if(cfg){
				/* escreve as configurações padrão */
				/* lê e define o fator de calibração */
				temp = readLine();
				cal_param = (float)temp.toInt();
				/* lê define o timeout da amostragem */
				temp = readLine();
				timeout = temp.toInt();
				Serial.println("Configuracoes feitas!");
				cfg.close();
			}
			else{
				Serial.println("Erro ao abrir arquivo de configuracao!");
			}
		}
		else{
			/* se não existir arquivo de cfg, cria um novo */
			cfg = SD.open("/config.txt", FILE_WRITE);
			if(cfg){
				/* escreve os valores padrão salvos hardcoded nas variaveis globais */
				cfg.println((String)cal_param);
				cfg.println((String)timeout);
				Serial.println("Arquivo de configuracao criado com os valores padrão.");
				cfg.close();
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
String readLine(void){
	char c;
	String line = "";
	while(true){
		c = cfg.read();
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
	loadCell.set_scale(cal_param);
	loadCell.tare();
	Serial.println("Celula de carga iniciada.");
}
/**
 * @brief Escreve o arquivo de log da amostragem atual.
 */
void SDWriteLog(void){

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
		if (strcmp((char*)data, "timestamp") == 0){
			timestamp = (char*)data;
		}
		else if(strcmp((char*)data, "sample_begin") == 0){
			/*sample_begin = true; */
		}
		else if(strcmp((char*)data, "set_config") == 0){

		}
		else if(strcmp((char*)data, "calibrate") == 0){

		}
	}
}
/**
 * Envia mensagens para todos os clientes.
 */
void notifyClients(String message) {
	ws.textAll(message);
}

bool sample(uint16_t timeout){
	uint16_t last_timer_count_value = timer_count;
	uint16_t n = 0;
	long read;
	long max_value = 0;
	long read_values[2400];
	uint16_t time[2400];
	sample_isrunning = true;
	/* captura a string com a data e o horario */
	/* abre um arquivo de log na pasta /tests com o nome = data-hora */

	while((timer_count - last_timer_count_value) <= timeout){
		read = loadCell.get_units(1) * gravity;
		if(read >= 10000.0){
			read_values[n] = read;
			time[n] = timer_count - last_timer_count_value;
			/* salva a leitura no cartao sd */
			/* envia uma string ou json contendo o valor */
			if(read_values[n] > max_value)
				max_value = read_values[n];
		}
		n++;
	}
	/* faz os calculos restantes */
	/* escreve */
	/* fecha o arquivo de log */
	/* envia mensagem de término */
	sample_isrunning = false;
	return true;
}

void setTimer(void){
	hw_timer_t *Timer0_Cfg = NULL;
	Timer0_Cfg = timerBegin(0, 80, true);
	timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
    timerAlarmWrite(Timer0_Cfg, 1000, true);
    timerAlarmEnable(Timer0_Cfg);
}

bool setConfig(String new_cfg){

}