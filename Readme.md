# SX126x_Async
This is an Arduino library for LoRa Communication using the radio transceiver chips [SX1268](https://www.semtech.com) and [SX1262](https://www.semtech.com).
This repository is a fork of the work done by tinytronix here: [tinytronix/SX126x](https://github.com/tinytronix/SX126x).  So thank you to tinytronix for laying the ground work on this library. 

I have adapted tinytronix's work to make this library better handle transmitting and receiving Asynchronously.  This is especially useful since LoRa is known for its low bitrate when high sensitivity is required.  Additional changes have been made to ensure the SX126x modules work well in scenarios where high sensitivity is required.

I chose to fork tinytronix's library because it is very minimal and provided more than enough functionality to facilitate point to point communicaitons. I will personally be using this library for use in high altitude weather balloons as a back-up long range transceiver for now. 

Most of the hardware driver software is taken from [RadioLib](https://github.com/jgromes/RadioLib) a universal wireless communication library for Arduino (great project!) and slightly adapted.

## Installing
Download this repo as zip. Then in the Arduino IDE go to Sketch->Add library->add .zip library.
Or please refer to the offical Arduino howto.

## First Steps
The first place to start may be the [examples](https://github.com/SaberShip/SX126x_Async/tree/master/examples) folder. There is a simple rx tx example for synchronous and asynchronous operation which may help you get started. 

## Hardware compatibility
This library was tested with an basic Arduino UNO connected to an Ebyte E22-900M30S LoRa module via SPI. [Module](https://www.ebyte.com/en/product-view-news.aspx?id=453)  This module uses the SX1262 and includes a built in TCXO for more stable frequencies over a range of temperatures.

Since this library supports Async operations for sending and receiving, an interrupt pin is required for pin DIO1 of the SX126x.  The library handles binding of this pin to an internal interrupt.  The library exposes TxDone and RxDone callbacks/hooks, all you have to do is provide the library with your callback function. See the async examples for more information.

## FAQ
Q: Why is the SW pin not supported by this library? <br>
A: Currently (in my hardware setup) the SW Pin is connected to 3,3V permanently, so RF is always on. In one of the next versions it might be a good idea to add a 5th parameter to the constructor (bool true/false) in order to let the SX126x DIO2 output control the RF switch. 5th Param TRUE: DIO2 switches RF, 5th Param FALSE: RF controlled externally. See SX126x datasheet, section "SetDio2AsRfSwitchCtrl" for details.<br><br>
<br>
Q: Does the lib support LoRaWAN?<br>
A: It is a bare metal driver library for SX126x chipset and implements clean LoRa data send and receive functons according to the OSI reference model. Therefore any LoRaWAN library may use this hardware driver library.<br> 
<br> 
Q: Is FSK Mode available.<br>
A: The SX126x chip implements FSK but it is not supported by this driver library.
