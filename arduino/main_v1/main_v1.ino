/*
 * The main scipt running the speed controller
 */

#include <ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Int16.h>
#include <arduino_feedback/feedback.h>
#include "encoders.h"
#include "fixedpoint.h"
#include "PID.h"
#include "WheelController.h"
#include <SPI.h>


#define M1pin 12
#define M2pin 11
#define STOP 126  //was 128 at some point...
#define ledPin 13

char log_msg[50] = {0};
char hello[13] = "hello world!";
int test_counter = 0;

//OBJECT INSTANCES
WheelController wheelCtrl1(0x00003550,0x00001500,0x00000020);
WheelController wheelCtrl2(0x00003550,0x00001500,0x00000020);

//INTERRUPT VARIABLES
int timer3_counter;
int ISR3_counter=0;

//ENCODER VARIABLES
signed long encoder1count = 0;
signed long encoder2count = 0;

//PID VARIABLES:
boolean PID_flag = false;
int RPM_ref_m1;
int RPM_actual_m1;
int RPM_ref_m2;
int RPM_actual_m2;
int PID_count;

//CALLBACK FROM /rpm_cmd TOPIC
void rpm_cmdCb( const std_msgs::Int16& msg){
  digitalWrite(13, HIGH-digitalRead(13));   // blink the led
  RPM_ref_m1 = msg.data;
  RPM_ref_m2 = RPM_ref_m1;
}

ros::NodeHandle  nh;
std_msgs::String str_msg;
arduino_feedback::feedback arduino_msg;
ros::Publisher arduino_chatter("arduino_data", &arduino_msg);
ros::Subscriber<std_msgs::Int16> sub("rpm_cmd", &rpm_cmdCb );



void setup()
{
  nh.initNode();
  nh.advertise(arduino_chatter);
  nh.subscribe(sub);
  
  initEncoders();       //Serial.println("Encoders Initialized...");  
  clearEncoderCount();  //Serial.println("Encoders Cleared...");
  pinMode(ledPin, OUTPUT);
  timer3_interrupt_setup(); //encoders read and RPM calcs
 // PWM SETTINGS (set timer 1 to 8 prescale to get a PWM with freq )
  TCCR1B = TCCR1B & B11111000 | B00000001; 
  pinMode(M1pin, OUTPUT);
  pinMode(M2pin, OUTPUT);
    RPM_ref_m1 = 0;
    RPM_ref_m2 = 0;
    analogWrite(M1pin, STOP);  
    analogWrite(M2pin, STOP);
    interrupts();


}

void loop()
{
  //update rpm reference commands
  nh.spinOnce();
  sprintf(log_msg, "rpm_cmd: %d", RPM_ref_m1);
  nh.loginfo(log_msg);
  
  // compute PID gains and write to motors when ready
    if (PID_flag)
     {
        PID_flag = false;
        analogWrite(M1pin, wheelCtrl1.tick(RPM_actual_m1, RPM_ref_m1));
        analogWrite(M2pin, wheelCtrl2.tick(-RPM_actual_m2, RPM_ref_m2));
     }
  
   // publish arduino data
    arduino_chatter.publish( &arduino_msg );


}



void timer3_interrupt_setup()
{
  // initialize timer1 
  noInterrupts();           // disable all interrupts
  TCCR3A = 0;
  TCCR3B = 0;

  // Set timer1_counter to the correct value for our interrupt interval
  //timer3_counter = 64911;   // preload timer 65536-16MHz/256/100Hz
  //timer3_counter = 3036;    // 3036 gives 0.5Hz ints at 256
  //timer3_counter = 59286;   //10hz ints at 256 prescale
  //timer3_counter = 40536;   //40536: 10hz ints at 64 prescale
  //timer3_counter = 45536;     //100hz at 8 prescale
    timer3_counter = 55536;   //200Hz at 8 prescale
  
  TCNT3 = timer3_counter;   // preload timer
  TCCR3B &=~7; //clear
  TCCR3B |= (1 << CS11);    //8 prescaler
  //TCCR3B |= (1 << CS12);    // 256 prescaler 
 //TCCR3B |= (1 << CS11);    // 64 prescaler 
 //TCCR3B |= (1 << CS10);    // 64 prescaler 
  TIMSK3 |= (1 << TOIE3);   // enable timer overflow interrupt
}


  
ISR(TIMER3_OVF_vect)        // interrupt service routine at 100Hz
{

  TCNT3 = timer3_counter;   // preload timer

  encoder1count = readEncoder(1); 
  encoder2count = readEncoder(2);

  ISR3_counter++;

  if (ISR3_counter >=2)   //at 100Hz
  {    

    ISR3_counter = 0;
    RPM_actual_m1 = (encoder1count*6000)/1920;
    RPM_actual_m2 = (encoder2count*6000)/1920;
    arduino_msg.encoder1 = test_counter;
    arduino_msg.encoder2 = encoder2count;
    arduino_msg.rpm1 = test_counter;
    arduino_msg.rpm2 = RPM_actual_m2;
    test_counter++;

    encoder1count = 0;
    encoder2count = 0;
    clearEncoderCount(); 
    PID_flag = true;

  }

}