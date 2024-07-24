var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
}

function getReadings(){
    websocket.send("getReadings");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

// When websocket is established, call the getReadings() function
function onOpen(event) {
    console.log('Connection opened');
    getReadings();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Create Temperature Chart
var chartT = new Highcharts.Chart({
    chart:{
      renderTo:'chart-temperature'
    },
    series: [
      {
        name: 'Temperature',
        type: 'line',
        color: '#00A6A6',
        marker: {
          symbol: 'square',
          radius: 3,
          fillColor: '#00A6A6',
        }
      },
      {
        name: 'Humidity',
        type: 'line',
        color: '#8B2635',
        marker: {
          symbol: 'triangle',
          radius: 3,
          fillColor: '#8B2635',
        }
      },
      {
        name: 'Pressure',
        type: 'line',
        color: '#71B48D',
        marker: {
          symbol: 'triangle-down',
          radius: 3,
          fillColor: '#71B48D',
        }
      },
    ],
    title: {
      text: undefined
    },
    xAxis: {
      type: 'datetime',
      dateTimeLabelFormats: { millisecond: '%H:%M:%S.%L' }
    },
    yAxis: {
      title: {
        text: 'Y Axis'
      }
    },
    credits: {
      enabled: false
    }
  });

// Function that receives the message from the ESP32 with the readings
function onMessage(event) {
    console.log(event.data);
    var myJSONObject = JSON.parse(event.data);
    var keys = Object.keys(myJSONObject);

    // for (var i = 0; i < keys.length; i++){
    //     var key = keys[i];
    //     document.getElementById(key).innerHTML = myJSONObject[key];
    // }

    plotTemperature(myJSONObject);
}

//Plot temperature in the temperature chart
function plotTemperature(jsonValue) {

    var keys = Object.keys(jsonValue);
    console.log(keys);
    console.log(keys.length);
  
    for (var i = 1; i < keys.length; i++){
      //var x = (new Date()).getTime();
      console.log(t);
      const epochKey = keys[0];
      const key = keys[i];
      var t = Number(jsonValue[epochKey]);
      var y = Number(jsonValue[key]);
      console.log(y);
  
      if(chartT.series.length > 40) {
        chartT.series[i-1].addPoint([t, y], true, true, true);
      } else {
        chartT.series[i-1].addPoint([t, y], true, false, true);
      }
  
    }
  }