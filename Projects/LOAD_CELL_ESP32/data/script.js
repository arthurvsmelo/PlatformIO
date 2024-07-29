// VARIAVEIS
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);
var sample_value = [];
var sample_time = [];

// retorna uma string com a data e a hora, para servir de nome do arquivo de amostras 
function getDate(){
    var date = new Date;
    const options = {
    month: 'numeric',
    day: 'numeric',
    };
    return date.toLocaleDateString("pt-Br", options).replace("/", "");
}

function getTime(){
    var date = new Date;
    const options = {
        hour: "numeric",
        minute: "numeric"
        };
        return date.toLocaleTimeString("pt-Br", options).replace(":", "");
}
// Atualiza a data e a hora a cada segundo, para ser exibida na página html
function updateDateTime() {
    const now = new Date();
    const datetimeDiv = document.getElementById('datetime');
    const options = { 
        weekday: 'short', year: 'numeric', month: 'long', 
        day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' 
    };
    datetimeDiv.textContent = now.toLocaleDateString('pt-BR', options);
}

function onLoad(event) {
    initWebSocket();
    initButton();
}
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    // chama essas funções de callback quando ocorrer esses eventos
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    if (websocket.readyState == 1) {
        getSdStatus();
        getSampleStatus();
    }
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 1000);
}

function onMessage(event) {
    console.log(event.data);
    var msg = JSON.parse(event.data);
    console.log(msg);
    if(msg.type == "sample"){
        document.getElementById("last_reading").innerHTML = msg.data.reading;
    }
    else if(msg.type == "sd_status"){
        if(msg.data == 0){
            document.getElementById("sd_info").innerHTML = "Error!";
        }
        else{
            document.getElementById("sd_info").innerHTML = "Ok";
        }
        
    }
    else if(msg.type == "sample_running"){
        if(msg.data == 0){
            document.getElementById("status_info").innerHTML = "StandBy";
        }
        else{
            document.getElementById("status_info").innerHTML = "Running";
        }
    }    
}

function initButton() {
    // botão de iniciar amostragem
    document.getElementById("sample_button").addEventListener("click", function(event){
        event.preventDefault();
        if(confirm("Deseja realmente iniciar?")){
            let date = getDate();
            let time = getTime();
            websocket.send(JSON.stringify({"type":"sample_begin", "data":{"date":date, "time":time}}));
            console.log("ws send: ", date);
            console.log("ws send: ", time);
        }
    });
}

function getSdStatus(){
    websocket.send(JSON.stringify({"type":"sd_status"}));
}
function getSampleStatus(){
    websocket.send(JSON.stringify({"type":"sample_running"}));
}
setInterval(updateDateTime, 1000);
updateDateTime();