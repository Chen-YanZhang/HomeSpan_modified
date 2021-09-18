
#include <Arduino.h>
#include <soc/rmt_reg.h>
#include <soc/dport_reg.h>

#include "RFControl.h"

///////////////////

RFControl::RFControl(int pin){
  if(!configured){            // configure RMT peripheral

    DPORT_REG_SET_BIT(DPORT_PERIP_CLK_EN_REG,1<<9);           // enable RMT clock by setting bit 9
    DPORT_REG_CLR_BIT(DPORT_PERIP_RST_EN_REG,1<<9);           // set RMT to normal ("un-reset") mode by clearing bit 9        
    REG_SET_BIT(RMT_APB_CONF_REG,3);                          // enables access to RMT memory and enables wraparound mode (though the latter does not seem to be needed to set continuous TX)
    REG_WRITE(RMT_INT_ENA_REG,1<<RMT_CH0_TX_END_INT_ENA_S);   // enable end-transmission interrupt so that interrupt vector is called
    REG_WRITE(RMT_CH0CONF0_REG,0x08000000);                   // disable carrier wave; set channel 0 to use all 8 blocks of RMT memory       
//    esp_intr_alloc(ETS_RMT_INTR_SOURCE,0,eot_int,NULL,NULL);  // set RMT general interrupt vector

    configured=true;
  }

  this->pin=pin;

  pinMode(pin,OUTPUT);
  REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG+4*pin,87);                         // set GPIO OUTPUT of pin in GPIO_MATRIX to use RMT Channel-0 Output (=signal 87)
  REG_SET_FIELD(GPIO_FUNC0_OUT_SEL_CFG_REG+4*pin,GPIO_FUNC0_OEN_SEL,1);   // use GPIO_ENABLE_REG of pin (not RMT) to enable this channel
  REG_WRITE(GPIO_ENABLE_W1TC_REG,1<<pin);                                 // disable output on pin - enable only when started
      
}
 
///////////////////

void RFControl::start(uint8_t _numCycles, uint8_t tickTime){

  if(pCount%2==0)                                             // if next entry is lower 16 bits of 32-bit memory
    pRMT[pCount/2]=0;                                         // set memory to zero (end-marker)
  else
    pRMT[pCount/2]&=0xFFFF;                                   // else preserve lower 16 bits and zero our upper 16 bits
  
  REG_WRITE(GPIO_ENABLE_W1TS_REG,1<<pin);                     // enable output on pin
  numCycles=_numCycles;                                       // set number of cycles to repeat transmission
  REG_SET_FIELD(RMT_CH0CONF0_REG,RMT_DIV_CNT_CH0,tickTime);   // set one tick = 1 microsecond * tickTime (RMT will be set to use 1 MHz REF_TICK, not 80 MHz APB_CLK)    
  REG_WRITE(RMT_CH0CONF1_REG,0x0000000D);                     // use REF_TICK clock; reset xmit and receive memory address to start of channel; START TRANSMITTING!
  while(numCycles);                                           // wait while transmission in progress
  REG_WRITE(GPIO_ENABLE_W1TC_REG,1<<pin);                     // disable output on pin
}

///////////////////

void RFControl::clear(){
  pCount=0;
}

///////////////////

void RFControl::add(uint16_t onTime, uint16_t offTime){

  phase(onTime,HIGH);
  phase(offTime,LOW);  
}

///////////////////

void RFControl::phase(uint16_t numTicks, uint8_t phase){

  if(pCount==1023){                                            // maximum number of entries reached (saving one space for end-marker)
    Serial.print("\n*** ERROR: Can't add more than 1023 entries to RF Control Module\n\n");
  } else
  
  if(numTicks>32767 || numTicks<1){
    Serial.print("\n*** ERROR: Request to add RF Control entry with numTicks=");
    Serial.print(numTicks);
    Serial.print(" is out of allowable range: 1-32767\n\n");
  } else {

    int index=pCount/2;
    
    if(pCount%2==0)                                             
      pRMT[index]=numTicks | (phase?(1<<15):0);                                // load entry into lower 16 bits of 32-bit memory           
    else
      pRMT[index]=pRMT[index] & 0xFFFF | (numTicks<<16) | (phase?(1<<31):0);   // load entry into upper 16 bits of 32-bit memory, preserving lower 16 bits

    pCount++;
  }

}

///////////////////

void RFControl::eot_int(void *arg){
  numCycles--;
  REG_WRITE(RMT_INT_CLR_REG,~0);                      // interrupt MUST be cleared first; transmission re-started after (clearing after restart crestes havoc)
  if(numCycles)
    REG_WRITE(RMT_CH0CONF1_REG,0x0000000D);           // use REF_TICK clock; reset xmit and receive memory address to start of channel; re-start transmission
}    

///////////////////
    
boolean RFControl::configured=false;
volatile int RFControl::numCycles;
//int32_t *RFControl::pRMT=(uint32_t *)RMT_CHANNEL_MEM(0);
int RFControl::pCount=0;
