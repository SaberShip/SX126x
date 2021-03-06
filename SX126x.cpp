#include "SX126x.h"

SX126x* SX126x::module1_ptr = nullptr;
SX126x* SX126x::module2_ptr = nullptr;

void SX126x::DIO1_ISR_1(void) 
{
  module1_ptr->Dio1Interrupt();
}

void SX126x::DIO1_ISR_2(void) 
{
  module2_ptr->Dio1Interrupt();
}


SPISettings SX126x::SX126X_SPI_SETTINGS(8000000, MSBFIRST, SPI_MODE0);

SX126x::SX126x(int spiSelect, int reset, int busy, int interrupt)
{
  SX126x_SPI_SELECT = spiSelect;
  SX126x_RESET      = reset;
  SX126x_BUSY       = busy;
  SX126x_INT0       = interrupt;
  txActive          = false;

  pinMode(SX126x_SPI_SELECT, OUTPUT);
  pinMode(SX126x_RESET, OUTPUT);
  pinMode(SX126x_BUSY, INPUT);
  pinMode(SX126x_INT0, INPUT);

  digitalWrite(SX126x_RESET, HIGH);

  SPI.begin();
}

uint8_t SX126x::ModuleConfig(uint8_t packetType, uint32_t frequencyInHz, int8_t txPowerInDbm, uint8_t defaultMode) 
{
  if ( txPowerInDbm > 22 )
    txPowerInDbm = 22;
  if ( txPowerInDbm < -3 )
    txPowerInDbm = -3;

  uint8_t rv = ERR_NONE;
  DefaultMode = defaultMode;

  if ( module1_ptr == nullptr ) {
    module1_ptr = this;
    attachInterrupt(digitalPinToInterrupt(SX126x_INT0), DIO1_ISR_1, RISING);
  }
  else if ( module2_ptr == nullptr ) {
    module2_ptr = this;
    attachInterrupt(digitalPinToInterrupt(SX126x_INT0), DIO1_ISR_2, RISING);
  }
  else {
    Serial.println("SX126x: WARNING! This library only supports a max of 2 LoRa modules on a single host!");
  }
  
  Reset();
  SetStandby(SX126X_STANDBY_RC);
  
  uint8_t currentMode = GetCurrentMode();
  if ( currentMode != SX126X_STATUS_MODE_STDBY_RC )
  {
    Serial.println("SX126x: error, maybe no SPI connection?");
    return (currentMode == 0) ? ERR_UNKNOWN : ERR_INVALID_MODE;
  }

  if ( packetType == SX126X_PACKET_TYPE_LORA) {
    SetPacketType(SX126X_PACKET_TYPE_LORA); 
    //RadioSetModem( ( SX126x.ModulationParams.PacketType == PACKET_TYPE_GFSK ) ? MODEM_FSK : MODEM_LORA );
  }
  else {
    return ERR_UNSUPPORTED_MODE;
  }

  ClearDeviceErrors();
  SetRegulatorMode(SX126X_REGULATOR_DC_DC);
  SetDio2AsRfSwitchCtrl(true);
  SetDio3AsTcxoCtrl(SX126X_DIO3_OUTPUT_1_8, SX126X_TCXO_SETUP_TIME);
  
  Calibrate( SX126X_CALIBRATE_ALL_BLOCKS );
  uint16_t errors = GetDeviceErrors();

  if ( errors & SX126X_MASK_CALIB_ERR ) {
    Serial.print("SX126x: error, calibration failed: 0b");
    Serial.println(errors, BIN);
    rv = ERR_CALIBRATION_FAILED;
  }

  SetStandby(SX126X_STANDBY_RC); 
  SetBufferBaseAddress(0, 0);
  SetPaConfig(0x04, 0x07, 0x00, 0x01);
  SetPowerConfig(txPowerInDbm, SX126X_PA_RAMP_800U);
  SetRfFrequency(frequencyInHz);

  return rv;
}


uint8_t SX126x::LoRaBegin(uint8_t spreadingFactor, uint8_t bandwidth, uint8_t codingRate, uint16_t preambleLength, uint8_t headerMode, bool crcOn, bool invertIrq) 
{

  uint16_t chipsPerSymbol = pow( 2.0, spreadingFactor );
  SymbolRate = ((float)SX126X_LORA_BANDWIDTHS[bandwidth]) / ((float)chipsPerSymbol);

  //Serial.println("SX126x: SymbolRate: " + String(SymbolRate) + "bps");

  uint8_t ldro = SX126X_LORA_LOW_DATA_RATE_OPTIMIZE_OFF; //LowDataRateOptimize
  if ( 1.0/SymbolRate > SX126X_LORA_LOW_DATA_RATE_OPTIMIZE_THRESH )
  {
    //Serial.println("SX126x: Enabling Low Data Rate Optimize");
    ldro = SX126X_LORA_LOW_DATA_RATE_OPTIMIZE_ON; //LowDataRateOptimize
  }
  

  SetStopRxTimerOnPreambleDetect(false);
  SetLoRaSymbNumTimeout(0);
  SetModulationParams(spreadingFactor, bandwidth, codingRate, ldro);
  
  PacketParams[0] = (preambleLength >> 8) & 0xFF;
  PacketParams[1] = preambleLength;
  PacketParams[2] = headerMode;

  if ( crcOn )
    PacketParams[4] = SX126X_LORA_CRC_ON;
  else
    PacketParams[4] = SX126X_LORA_CRC_OFF;

  if ( invertIrq )
    PacketParams[5] = SX126X_LORA_IQ_STANDARD;
  else
    PacketParams[5] = SX126X_LORA_IQ_INVERTED;

  SPIwriteCommand(SX126X_CMD_SET_PACKET_PARAMS, PacketParams, 6);
  SetDioIrqParams(SX126X_IRQ_ALL,   //all interrupts enabled
                  SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, //interrupts on DIO1
                  SX126X_IRQ_NONE,  //interrupts on DIO2
                  SX126X_IRQ_NONE); //interrupts on DIO3
				  
  ClearIrqStatus(SX126X_IRQ_ALL);

  // Enter the default mode of operation
  EnterDefaultMode();
}


uint8_t SX126x::Receive(uint8_t *pData, uint16_t *len) 
{
  uint8_t rv = ERR_NONE;
  uint16_t irq = GetIrqStatus();
  
  if( irq & SX126X_IRQ_RX_DONE )
  {
    ClearIrqStatus(SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT);
    rv = ReadBuffer(pData, len);
  }
  else if ( irq & SX126X_IRQ_TIMEOUT) {
    rv = ERR_RX_TIMEOUT;
  }
  else {
    *len = 0;
    rv = ERR_NONE;
  }
  
  return rv;
}


uint8_t SX126x::Send(uint8_t *pData, uint16_t len, uint32_t timeoutInMs)
{
  uint16_t irq;
  bool rv = SendAsync(pData, len, timeoutInMs);

  if ( rv == ERR_NONE ) 
  {
    while ( txActive ) {}
    irq = GetIrqStatus();

    EnterDefaultMode();

    if ( irq & SX126X_IRQ_TIMEOUT ) {
      rv = ERR_TX_TIMEOUT;
    }
  }
	
	return rv;
}


uint8_t SX126x::SendAsync(uint8_t *pData, uint16_t len, uint32_t timeoutInMs) 
{
  bool rv = ERR_NONE;

  if ( txActive ) {
    rv = ERR_DEVICE_BUSY;
  }
  else 
  {
    PacketParams[3] = len;
	  SPIwriteCommand(SX126X_CMD_SET_PACKET_PARAMS, PacketParams, 6);

    rv = WriteBuffer(pData, len);
	  SetTx(timeoutInMs);

    txActive = true;
  }

  return rv;
}


uint8_t SX126x::ReceiveMode(uint32_t timeoutInMs)
{
  uint8_t rv = ERR_NONE;

  if ( txActive == true )
  {
    rv = ERR_DEVICE_BUSY;
  }
  else
  {
    uint8_t currentMode = GetCurrentMode();
    if ( !currentMode & SX126X_STATUS_MODE_RX ) {
      SetRx(timeoutInMs);
    }
  }

  return rv;
}


void SX126x::ReceiveStatus(int8_t *rssiPacket, int8_t *snrPacket)
{
    uint8_t buf[3];
     
    SPIreadCommand( SX126X_CMD_GET_PACKET_STATUS, buf, 3 );

    ( buf[1] < 128 ) ? ( *snrPacket = buf[1] >> 2 ) : ( *snrPacket = ( ( buf[1] - 256 ) >> 2 ) );
    *rssiPacket = -(buf[0] >> 1);
}


void SX126x::Reset(void)
{
  digitalWrite(SX126x_RESET, LOW);
  delayMicroseconds(600);
  digitalWrite(SX126x_RESET, HIGH);
  while(digitalRead(SX126x_BUSY));
}


void SX126x::Wakeup(void)
{
  GetStatus();
}


void SX126x::setTxDoneHook(void (*txHook)(uint8_t txStatus)) {
  __txDoneHook = txHook;
}


void SX126x::setRxDoneHook(void (*rxHook)(uint8_t rxStatus, uint8_t *pdata, uint16_t len)) {
  __rxDoneHook = rxHook;
}


void SX126x::Dio1Interrupt() 
{
  uint16_t irq = GetIrqStatus();
  if( txActive ) 
  {
    if ( irq & (SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT) ) 
    {
      txActive = false;
      EnterDefaultMode();

      if ( __txDoneHook != nullptr ) 
      {
        // uint16_t devErrors = GetDeviceErrors();
        // Serial.print("Tx Done getErrors = ");
        // Serial.println(devErrors, BIN);
        __txDoneHook((irq & SX126X_IRQ_TIMEOUT) ? ERR_TX_TIMEOUT : ERR_NONE);
      }
    }
  }
  else if ( irq & (SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT) ) 
  {
    if ( __rxDoneHook != nullptr ) 
    {
      uint16_t len = SX126X_BUFFER_SIZE;
      uint8_t* pRxData = new uint8_t[len];
      uint8_t rxStatus = Receive(pRxData, &len);
      __rxDoneHook(rxStatus, pRxData, len);
      free(pRxData);
    }
  }
}


uint8_t SX126x::GetCurrentMode(void) {
  return GetStatus() & 0x70;
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command SetStandby(...) is used to set the device in a configuration mode which is at an intermediate level of
//  consumption. In this mode, the chip is placed in halt mode waiting for instructions via SPI. This mode is dedicated to chip
//  configuration using high level commands such as SetPacketType(...).
//  By default, after battery insertion or reset operation (pin NRESET goes low), the chip will enter in STDBY_RC mode running
//  with a 13 MHz RC clock
//
//  Parameters
//  ----------
//  0: Device running on RC13M, set STDBY_RC mode
//  1: Device running on XTAL 32MHz, set STDBY_XOSC mode
//
//  Return value
//  ------------
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetStandby(uint8_t mode)
{
  uint8_t data = mode;
  SPIwriteCommand(SX126X_CMD_SET_STANDBY, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The host can retrieve chip status directly through the command GetStatus() : this command can be issued at any time and
//  the device returns the status of the device. The command GetStatus() is not strictly necessary since device returns status
//  information also on command bytes.
//
//  Parameters:
//  none
//
//  Return value:
//  Bit 7: unused
//  Bit 6:4 Chipmode
//  Bit 3:1 Command Status
//  Bit 0: unused
//----------------------------------------------------------------------------------------------------------------------------
uint8_t SX126x::GetStatus(void)
{
  uint8_t rv = 0x00;
  SPIreadCommand(SX126X_CMD_GET_STATUS, &rv, 1);
  return rv;
}


//----------------------------------------------------------------------------------------------------------------------------
//  The BUSY line is mandatory to ensure the host controller is ready to accept SPI commands.
//  When BUSY is high, the host controller must wait until it goes down again before sending another command.
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::WaitOnBusy( void )
{
  while( digitalRead(SX126x_BUSY) == 1 );
}



//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetDio3AsTcxoCtrl(uint8_t tcxoVoltage, uint32_t timeoutInMs)
{
    uint8_t buf[4];
    uint32_t tout = timeoutInMs << 6;  // convert from ms to SX126x time base

    buf[0] = tcxoVoltage & 0x07;
    buf[1] = ( uint8_t )( ( tout >> 16 ) & 0xFF );
    buf[2] = ( uint8_t )( ( tout >> 8 ) & 0xFF );
    buf[3] = ( uint8_t )( tout & 0xFF );

    SPIwriteCommand(SX126X_CMD_SET_DIO3_AS_TCXO_CTRL, buf, 4);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::Calibrate(uint8_t calibParam)
{
  uint8_t data = calibParam;
  SPIwriteCommand(SX126X_CMD_CALIBRATE, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetDio2AsRfSwitchCtrl(uint8_t enable)
{
  uint8_t data = enable;
  SPIwriteCommand(SX126X_CMD_SET_DIO2_AS_RF_SWITCH_CTRL, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetRfFrequency(uint32_t frequency)
{
  uint8_t buf[4];
  uint32_t freq = 0;

  CalibrateImage(frequency);

  freq = (uint32_t)((double)frequency / (double)SX126X_FREQ_STEP);
  buf[0] = (uint8_t)((freq >> 24) & 0xFF);
  buf[1] = (uint8_t)((freq >> 16) & 0xFF);
  buf[2] = (uint8_t)((freq >> 8) & 0xFF);
  buf[3] = (uint8_t)(freq & 0xFF);
  SPIwriteCommand(SX126X_CMD_SET_RF_FREQUENCY, buf, 4);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::CalibrateImage(uint32_t frequency)
{
  uint8_t calFreq[2];

  if( frequency > 900000000 )
  {
      calFreq[0] = SX126X_CAL_IMG_902_MHZ_1;
      calFreq[1] = SX126X_CAL_IMG_902_MHZ_2;
  }
  else if( frequency > 850000000 )
  {
      calFreq[0] = SX126X_CAL_IMG_863_MHZ_1;
      calFreq[1] = SX126X_CAL_IMG_863_MHZ_2;
  }
  else if( frequency > 770000000 )
  {
      calFreq[0] = SX126X_CAL_IMG_779_MHZ_1;
      calFreq[1] = SX126X_CAL_IMG_779_MHZ_2;
  }
  else if( frequency > 460000000 )
  {
      calFreq[0] = SX126X_CAL_IMG_470_MHZ_1;
      calFreq[1] = SX126X_CAL_IMG_470_MHZ_2;
  }
  else if( frequency > 425000000 )
  {
      calFreq[0] = SX126X_CAL_IMG_430_MHZ_1;
      calFreq[1] = SX126X_CAL_IMG_430_MHZ_2;
  }
  SPIwriteCommand(SX126X_CMD_CALIBRATE_IMAGE, calFreq, 2);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetRegulatorMode(uint8_t mode)
{
    uint8_t data = mode;
    SPIwriteCommand(SX126X_CMD_SET_REGULATOR_MODE, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetBufferBaseAddress(uint8_t txBaseAddress, uint8_t rxBaseAddress)
{
    uint8_t buf[2];

    buf[0] = txBaseAddress;
    buf[1] = rxBaseAddress;
    SPIwriteCommand(SX126X_CMD_SET_BUFFER_BASE_ADDRESS, buf, 2);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetPowerConfig(int8_t power, uint8_t rampTime)
{
    uint8_t buf[2];

    if( power > 22 )
    {
        power = 22;
    }
    else if( power < -3 )
    {
        power = -3;
    }
    
    buf[0] = power;
    buf[1] = ( uint8_t )rampTime;
    SPIwriteCommand(SX126X_CMD_SET_TX_PARAMS, buf, 2);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetPaConfig(uint8_t paDutyCycle, uint8_t hpMax, uint8_t deviceSel, uint8_t paLut)
{
    uint8_t buf[4];

    buf[0] = paDutyCycle;
    buf[1] = hpMax;
    buf[2] = deviceSel;
    buf[3] = paLut;
    SPIwriteCommand(SX126X_CMD_SET_PA_CONFIG, buf, 4);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The OCP is configurable by steps of 2.5 mA and the default value is re-configured automatically each time the function
//  SetPaConfig(...) is called. If the user wants to adjust the OCP value, it is necessary to change the register as a second 
//  step after calling the function SetPaConfig.
//
//  Parameters:
//  value: steps of 2,5mA (0x18 = 60mA)
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetOvercurrentProtection(uint8_t value)
{
  uint8_t buf[3];

  buf[0] = ((SX126X_REG_OCP_CONFIGURATION & 0xFF00) >> 8);
  buf[1] = (SX126X_REG_OCP_CONFIGURATION & 0x00FF);
  buf[2] = value;
  SPIwriteCommand(SX126X_CMD_WRITE_REGISTER, buf, 3);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetDioIrqParams( uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask )
{
    uint8_t buf[8];

    buf[0] = (uint8_t)((irqMask >> 8) & 0x00FF);
    buf[1] = (uint8_t)(irqMask & 0x00FF);
    buf[2] = (uint8_t)((dio1Mask >> 8) & 0x00FF);
    buf[3] = (uint8_t)(dio1Mask & 0x00FF);
    buf[4] = (uint8_t)((dio2Mask >> 8) & 0x00FF);
    buf[5] = (uint8_t)(dio2Mask & 0x00FF);
    buf[6] = (uint8_t)((dio3Mask >> 8) & 0x00FF);
    buf[7] = (uint8_t)(dio3Mask & 0x00FF);
    SPIwriteCommand(SX126X_CMD_SET_DIO_IRQ_PARAMS, buf, 8);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetStopRxTimerOnPreambleDetect( bool enable )
{
  uint8_t data = (uint8_t)enable;
  SPIwriteCommand(SX126X_CMD_STOP_TIMER_ON_PREAMBLE, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  In LoRa mode, when going into Rx, the modem will lock as soon as a LoRa® symbol has been detected which may lead to
//  false detection. This phenomena is quite rare but nevertheless possible. To avoid this, the command
//  SetLoRaSymbNumTimeout can be used to define the number of symbols which will be used to validate the correct
//  reception of a packet.
//
//  Parameters:
//  0:      validate the reception as soon as a LoRa® Symbol has been detected
//  1..255: When SymbNum is different from 0, the modem will wait for a total of SymbNum LoRa® symbol to validate, or not, the
//          correct detection of a LoRa packet. If the various states of the demodulator are not locked at this moment, the radio will
//          generate the RxTimeout IRQ.
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetLoRaSymbNumTimeout(uint8_t SymbNum)
{
  uint8_t data = SymbNum;
  SPIwriteCommand(SX126X_CMD_SET_LORA_SYMB_NUM_TIMEOUT, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetPacketType(uint8_t packetType)
{
    uint8_t data = packetType;
    SPIwriteCommand(SX126X_CMD_SET_PACKET_TYPE, &data, 1);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetModulationParams(uint8_t spreadingFactor, uint8_t bandwidth, uint8_t codingRate, uint8_t lowDataRateOptimize)
{
  uint8_t data[4];
  //currently only LoRa supported
  data[0] = spreadingFactor;
  data[1] = bandwidth;
  data[2] = codingRate;
  data[3] = lowDataRateOptimize;
  SPIwriteCommand(SX126X_CMD_SET_MODULATION_PARAMS, data, 4);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
uint16_t SX126x::GetIrqStatus( void )
{
    uint8_t data[2];
    SPIreadCommand(SX126X_CMD_GET_IRQ_STATUS, data, 2);
    return (data[0] << 8) | data[1];
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::ClearIrqStatus(uint16_t irq)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(((uint16_t)irq >> 8) & 0x00FF);
    buf[1] = (uint8_t)((uint16_t)irq & 0x00FF);
    SPIwriteCommand(SX126X_CMD_CLEAR_IRQ_STATUS, buf, 2);
}


uint16_t SX126x::GetDeviceErrors(void) {
  uint8_t data[2];
  SPIreadCommand(SX126X_CMD_GET_DEVICE_ERRORS, data, 2);
  return (data[0] << 8) | data[1];
}


void SX126x::ClearDeviceErrors(void) {
  uint8_t data;
  SPIreadCommand(SX126X_CMD_CLEAR_DEVICE_ERRORS, &data, 1);
}


void SX126x::EnterDefaultMode(void) 
{
  switch(DefaultMode) {
    case(SX126X_DEFAULT_MODE_STBY_XOSC):
      SetStandby(SX126X_STANDBY_XOSC);
      break;

    case(SX126X_DEFAULT_MODE_FS):
      SetFs();
      break;

    case(SX126X_DEFAULT_MODE_RX_CONTINUOUS):
      SetRx(SX126X_RX_NO_TIMEOUT_CONT);
      break;

    case(SX126X_DEFAULT_MODE_RX_SINGLE):
      SetRx(SX126X_RX_NO_TIMEOUT_SINGLE);
      break;

    case(SX126X_DEFAULT_MODE_STBY_RC):
    default:
      SetStandby(SX126X_STANDBY_RC);
      break;
  }
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetRx(uint32_t timeoutInMs)
{
  ClearIrqStatus(SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT);

  uint32_t tout = 0;
  if ( timeoutInMs >= SX126X_RX_NO_TIMEOUT_CONT ) 
  {
    tout = SX126X_RX_NO_TIMEOUT_CONT;
  }
  else if (timeoutInMs > 0)
  {
    tout = timeoutInMs << 6;  // convert from ms to SX126x time base
  }

  uint8_t buf[3];
  buf[0] = (uint8_t)((tout >> 16) & 0xFF);
  buf[1] = (uint8_t)((tout >> 8) & 0xFF);
  buf[2] = (uint8_t )(tout & 0xFF);
  SPIwriteCommand(SX126X_CMD_SET_RX, buf, 3);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command SetTx() sets the device in transmit mode. When the last bit of the packet has been sent, an IRQ TX_DONE
//  is generated. A TIMEOUT IRQ is triggered if the TX_DONE IRQ is not generated within the given timeout period.
//  The chip goes back to STBY_RC mode after a TIMEOUT IRQ or a TX_DONE IRQ.
//  The timeout duration can be computed with the formula: Timeout duration = Timeout * 15.625 μs 
//  The Timeout duration is expected in ms, so the desired formula becomes tout = timeoutInMs << 6
//
//  Parameters:
//  0: Timeout disable, Tx Single mode, the device will stay in TX Mode until the packet is transmitted
//  other: Timeout in milliseconds, timeout active, the device remains in TX mode. The maximum timeout is then 262 s.
//  
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::SetTx(uint32_t timeoutInMs)
{  
  ClearIrqStatus(SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT);

  uint32_t tout = 0;
  if (timeoutInMs > 0) 
  {
    tout = timeoutInMs << 6;  // convert from ms to SX126x time base
  }
  uint8_t buf[3];
  buf[0] = (uint8_t)((tout >> 16) & 0xFF);
  buf[1] = (uint8_t)((tout >> 8) & 0xFF);
  buf[2] = (uint8_t) (tout & 0xFF);
  SPIwriteCommand(SX126X_CMD_SET_TX, buf, 3);
}


void SX126x::SetFs(void) 
{
  uint8_t buf = 0;
  SPIwriteCommand(SX126X_CMD_SET_FS, &buf, 0);
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
void SX126x::GetRxBufferStatus(uint8_t *payloadLength, uint8_t *rxStartBufferPointer)
{
    uint8_t buf[2];

    SPIreadCommand( SX126X_CMD_GET_RX_BUFFER_STATUS, buf, 2 );
	
    *payloadLength = buf[0];
    *rxStartBufferPointer = buf[1];
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
uint8_t SX126x::ReadBuffer(uint8_t *rxData, uint16_t *rxDataLen)
{
  uint8_t rv = ERR_NONE;
  uint8_t offset = 0;
  uint8_t packetLen = 0;
  
  GetRxBufferStatus(&packetLen, &offset);
  if( packetLen > *rxDataLen)
  {
    // Packet is to large to fit in supplied buffer, read what we can
    rv = ERR_PACKET_TOO_LONG;
  }
  else {
    *rxDataLen = packetLen;
  }

  while(digitalRead(SX126x_BUSY));
  
  digitalWrite(SX126x_SPI_SELECT, LOW);
  SPI.beginTransaction(SX126X_SPI_SETTINGS);
  SPI.transfer(SX126X_CMD_READ_BUFFER);
  SPI.transfer(offset);
  SPI.transfer(SX126X_CMD_NOP);
  for( uint8_t i = 0; i < *rxDataLen; i++ )
  {
    rxData[i] = SPI.transfer(SX126X_CMD_NOP);
  }
  digitalWrite(SX126x_SPI_SELECT, HIGH);
  
  while(digitalRead(SX126x_BUSY));

  return rv;
}


//----------------------------------------------------------------------------------------------------------------------------
//  The command...
//
//  Parameters:
//  none
//
//
//  Return value:
//  none
//  
//----------------------------------------------------------------------------------------------------------------------------
uint8_t SX126x::WriteBuffer(uint8_t *txData, uint16_t txDataLen)
{
  uint8_t rv = ERR_NONE;

  if (txDataLen > SX126X_BUFFER_SIZE) {
    rv = ERR_PACKET_TOO_LONG;
  }

  digitalWrite(SX126x_SPI_SELECT, LOW);
  SPI.beginTransaction(SX126X_SPI_SETTINGS);
  SPI.transfer(SX126X_CMD_WRITE_BUFFER);
  SPI.transfer(0); //offset in tx fifo
  for( uint16_t i = 0; i < txDataLen; i++ )
  {
      SPI.transfer(txData[i]);  
  }
  digitalWrite(SX126x_SPI_SELECT, HIGH);

  while(digitalRead(SX126x_BUSY));

  return rv;
}


void SX126x::SPIwriteCommand(uint8_t cmd, uint8_t* data, uint8_t numBytes, bool waitForBusy) {
  SPItransfer(cmd, true, data, NULL, numBytes, waitForBusy);
}


void SX126x::SPIreadCommand(uint8_t cmd, uint8_t* data, uint8_t numBytes, bool waitForBusy) {
  SPItransfer(cmd, false, NULL, data, numBytes, waitForBusy);
}


void SX126x::SPItransfer(uint8_t cmd, bool write, uint8_t* dataOut, uint8_t* dataIn, uint8_t numBytes, bool waitForBusy) {
  
  // ensure BUSY is low (state meachine ready)
  // TODO timeout
  while(digitalRead(SX126x_BUSY));

  // start transfer
  digitalWrite(SX126x_SPI_SELECT, LOW);
  SPI.beginTransaction(SX126X_SPI_SETTINGS);

  // send command byte
  SPI.transfer(cmd);

  // send/receive all bytes
  if(write) {
    // Serial.print("SPI write: CMD=0x");
    // Serial.print(cmd, HEX);
    // Serial.print(" DataOut: ");
    for(uint8_t n = 0; n < numBytes; n++) {
      uint8_t in = SPI.transfer(dataOut[n]);
      // Serial.print(dataOut[n], HEX);
      // Serial.print(" ");
    }
    //Serial.println();
  } else {
    // Serial.print("SPI read:  CMD=0x");
    // Serial.print(cmd, HEX);
    // skip the first byte for read-type commands (status-only)
    uint8_t in = SPI.transfer(SX126X_CMD_NOP);
    // Serial.println((SX126X_CMD_NOP, HEX));
    // Serial.print(" DataIn: ");

    for(uint8_t n = 0; n < numBytes; n++) {
      dataIn[n] = SPI.transfer(SX126X_CMD_NOP);
      // Serial.println((SX126X_CMD_NOP, HEX));
      // Serial.print(dataIn[n], HEX);
      // Serial.print(" ");
    }
    // Serial.println();
  }

  // stop transfer
  SPI.endTransaction();
  digitalWrite(SX126x_SPI_SELECT, HIGH);

  // wait for BUSY to go high and then low
  // TODO timeout
  if(waitForBusy) {
    delayMicroseconds(1);
    while(digitalRead(SX126x_BUSY));
  }
}