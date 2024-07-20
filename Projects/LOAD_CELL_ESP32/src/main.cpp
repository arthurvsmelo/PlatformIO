#include <Arduino.h>
#include <HX711.h>
#include <esp32-hal-gpio.h>
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

HX711 loadCell;
float cal_param = -268.20;
const float gravity = 9.789;
int timeout = 10000;                /* timeout em milissegundos */
File cfg;
File log;
String timestamp;
bool sample_begin;
/* Usuário e senha da rede local wifi */
const char* ssid = "ESP32_AP";
const char* password = "decola_asa";
/* Inicia servidor assíncrono */
AsyncWebServer server(80);
/* Inicia comunicação via websocket */
AsyncWebSocket ws("/ws");

/* Protótipo de Funções */
void calibrate(float weight);
bool SDCardInit(void);
void checkSDconfig(void);
void SDWriteLog(void);
void gpio_init(void);
String readLine(void);
void loadCellInit(void);
void handleRoot(void);
void handleCSS(void);
void handleJS();
bool spiffsInit(void);
void handleNotFound(void);
void wifiInit(void);
void serverInit(void);


void setup() {
	Serial.begin(115200);
	gpio_init();
	loadCellInit();
	wifiInit();
	serverInit();	
}

void loop() {
	long read = loadCell.get_units(1) * gravity;
	while(timeout--){
		if(read >= 10000.0){
		Serial.println(read);
		}
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
 * @brief Inicializa todos os pinos necessários
 */
void gpio_init(void){
	/* Pino CS (chip-select) precisa ser definido como OUTPUT para a biblioteca
	   do cartão SD funcionar corretamente. */
	gpio_set_direction(CS, GPIO_MODE_OUTPUT);
}
/**
 * @brief Inicializa o cartão SD. 
 * 
 */
bool SDCardInit(void){
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
	if(SDCardInit()){
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
void loadCellInit(void){
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
 * @brief Função que envia a página html pela rota root.
 */
void handleRoot(void) {
	File file = LittleFS.open("/index.html", "r");
  	if (!file) {
    	server.send(500, "text/plain", "Internal Server Error");
    	return;
  	}
  	String html = file.readString();
  	server.send(200, "text/html", html);
  	file.close();
}
/**
 * @brief Função que envia a parte CSS da página html.
 */
void handleCSS(void) {
  	File file = SPIFFS.open("/style.css", "r");
  	if (!file) {
    	server.send(500, "text/plain", "Internal Server Error");
    	return;
  	}
  	String css = file.readString();
  	server.send(200, "text/css", css);
  	file.close();
}
/**
 * @brief Função que envia a parte JavaScript da página html.
 */
void handleJS() {
  	File file = SPIFFS.open("/script.js", "r");
  	if (!file) {
    	server.send(500, "text/plain", "Internal Server Error");
    	return;
  	}
  	String js = file.readString();
  	server.send(200, "application/javascript", js);
  	file.close();
}
/**
 * @brief Handle de rota não encontrada.
 */
void handleNotFound(void) {
	/* Página para rota não encontrada */
	server.send(404, "text/plain", "404: Not found");
}
/**
 * @brief Inicia a WiFi como ponto de acesso.
 */
void wifiInit(void){
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
bool littleFS_init(void){
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
void initWebSocket(){
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
			sample_begin = true;
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