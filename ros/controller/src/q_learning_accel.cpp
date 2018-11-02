#include "ros/ros.h"
#include "std_msgs/String.h"
#include <stdio.h>
#include <wiringSerial.h>
#include <wiringPi.h>
#include <sensor_msgs/Imu.h>
#include <tf/LinearMath/Matrix3x3.h>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <iostream>
#include <numeric>


#include <q_model_install/Q_state.h>
#include "robot_teleop_tuner/pid_values.h"
#include "controller/Q_state.h"
#include "controller/State.h"
#include "controller/PidData.h"
#include <std_msgs/Int16.h>

#include <deque>
#include <vector>
#include <time.h>
#include <cmath>
#include <algorithm>

//uses the gazebo sim model
#define MODEL_READ 0
#define EPSILON 0.3
#define ALPHA 0.6
#define GAMMA 0.6

#define FREQUENCY 15
#define RL_DELTA 0.067
#define STOP_PWM 130
#define STOP_TORQUE 0

#define PITCH_FIX 5.5
#define REFERENCE_PITCH -1.0

#define PITCH_THRESHOLD 6.5
#define ACTIONS 7
#define ACTIONS_HALF 3
#define ACTION_BIAS 3
#define RUNNING_AVG 3

//The rpms below equal the following torques (N.m) respectively: { -0.61,-0.7,-0.75,0, 0.75, 0.7, 0.6}...torque of 0 = max(rpm) 
//int actions[ACTIONS] = {-75, -35, -13, 0, 13, 35, 75};	
//float actions[ACTIONS] = {-0.61, -0.7, 0, 0.7, 0.61};
//int actions[ACTIONS] = {100, 110, 120, 130, 140, 150, 160};
//float actions[ACTIONS] = {-0.5, -0.25, -0.127, 0, 0.127, 0.25, 0.5}; //max torque is torque required to counteract 10 deg fall. 
//these torque produce very high RPMs that are impracticle for this sytem. 
//float actions[ACTIONS] =  {-0.575, -0.643, -0.71, 0, 0.71, 0.643, 0.575};
//these actions produce the following RPMs: -90, -60, -30, 0, 30, 60, 90



//float actions[ACTIONS] =  {-0.643, -0.71, -0.73, 0, 0.73,  0.71, 0.643};
//float actions[ACTIONS] =  {0.71, 0.73, 0.755, 0, -0.755, -0.73,  -0.71};
//float actions[ACTIONS] =  {0.676, 0.71, 0.73,  0, -0.73,  -0.71, -0.676}; //rpms: 45, 30, 20
//NOTE: changed to using RPMs for convienence:
//float actions[ACTIONS] =  {-45, -30, -20,  0, 20,  30, 45}; //rpms: 45, 30, 20
float actions[ACTIONS] =  {-45, -30,-15,  0, 15,  30, 45}; //rpms: 45, 30, 20
//float actions[ACTIONS] =  {145, 140, 135, 130,  125, 120,  115}; 

#define WHEEL_RADIUS 0.19
#define MAX_EPISODE 150

// 2D state space
#define STATE_NUM_PHI 11
#define STATE_NUM_PHI_D 11
#define STATE_NUM_ACCEL 9
//T1
float phi_states[STATE_NUM_PHI] = {-5, -3, -2, -1, -0.5, 0, 0.5, 1, 2, 3, 5};
//float phi_d_states[STATE_NUM_PHI_D] = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5};
//float phi_states[STATE_NUM_PHI] = {-10, -7.5, -5, -2.5,-1, 0, 1, 2.5, 5, 7.5, 10};
//float phi_d_states[STATE_NUM_PHI_D] = {-15, -10, -7.5, -5, -2.5, 0, 2.5, 5, 7.5, 10, 15};
//float phi_d_states[STATE_NUM_PHI_D] = {-4, -3, -2, -1, -0.5,  0, 0.5, 1, 2, 3, 4};
//float phi_d_states_high_vel[STATE_NUM_PHI_D] = {-30, -25, -20, -15, 0, 15, 20, 25, 30};
float phi_d_states[STATE_NUM_PHI_D] = {-2, -1.5, -1, -0.6, -0.2,  0, 0.2, 0.6, 1, 1.5, 2};
float linear_accel_states[STATE_NUM_ACCEL] = {-2,-1.5, -1,-0.5, 0,0.5, 1,1.5, 2};

class Controller
{

	public:
		Controller();
		~Controller();
		void init();
		void pitch_tolerance();

		//ros stuff (subscribe to IMU data topic)	
  		ros::NodeHandle n;	//make private? eh..
		ros::Subscriber sub_imu;
		void IMU_callback(const sensor_msgs::Imu::ConstPtr& msg);
    	
		//serialPutchar parameter
		int fd;
		
		//IMU variables
		double roll;
		double pitch;
		double yaw;
		float pitch_dot_imu;
		bool motors;
		double linear_accel;

};



Controller::Controller()
	:	roll(0.0)
	    ,	pitch(0.0)
	    ,	yaw(0.0)
	    ,   motors(false)
{
	//Ros Init (subscribe to IMU topic)
	sub_imu = n.subscribe("imu/data", 1000, &Controller::IMU_callback, this);
	//Serial Init
/*	if ((fd = serialOpen("/dev/ttyACM0",BAUD_RATE))<0)
	{
		ROS_INFO("Unable to open serial device");
	}	
	if (wiringPiSetup() == -1)
	{
		ROS_INFO("Unable to start wiringPi");
	}*/
}

Controller::~Controller()
{
	ROS_INFO("Deleting node");
}


void Controller::IMU_callback(const sensor_msgs::Imu::ConstPtr& msg)
{
//	ROS_INFO("IMU Seq: [%d]", msg->header.seq);
//	ROS_INFO("IMU orientation x: [%f], y: [%f], z: [%f], w: [%f]", msg->orientation.x, msg->orientation.y, msg->orientation.z, msg->orientation.w);
	
	tf::Quaternion q(msg->orientation.x, msg->orientation.y, msg->orientation.z, msg->orientation.w);
	//double roll, pitch, yaw;
	tf::Matrix3x3(q).getRPY(roll, pitch, yaw);
	//ROS_INFO("the pitch in the callback is: %f", pitch*(180/M_PI));
	pitch = pitch*(180/M_PI) - PITCH_FIX;
	float pitch_dot_imu_rad;
	pitch_dot_imu_rad = (msg->angular_velocity.x);
	//ROS_INFO("pitch vel from IMU: %f", pitch_dot_imu_rad*(180/M_PI));
	pitch_dot_imu = -pitch_dot_imu_rad*(180/M_PI);
	linear_accel = msg->linear_acceleration.x;
}		


void Controller::pitch_tolerance()
{
	if (std::abs(pitch) < 0.5)
	{
		pitch = 0.0;
	}
}




class reinforcement_learning
{
  public:
    float alpha;
    int episode_num;
    int time_steps;
    int wins;
    int loses;
    float discount_factor;
    float epsilon;
    float pitch_dot;
    float prev_pitch;
    char current_state;
    char next_state;
    controller::State msg;
    float action;
    char action_idx;
    float reward_per_ep;
    ros::NodeHandle n;	//make private? eh..
    ros::Subscriber sub_q;
    std::deque<float> pitch_dot_data;
    int running_avg_cntr;
    float pitch_dot_filtered;	
    reinforcement_learning();
    ~reinforcement_learning();

    std::vector<std::vector<float> > Q;
    ros::Publisher q_state_publisher;


    char virtual choose_action(char) = 0;
    void TD_update(char, int, char, float);
    char get_state(float, float, float);
    //char get_next_state(float,float, char);
    float get_reward(float, float);
    void publishQstate(void);
    void read_model(void);
    void Q_callback(const q_model_install::Q_state::ConstPtr& q_model);
    int rpm_transform(void);
    float running_avg_pitch_dot(void);
};
reinforcement_learning::reinforcement_learning()
  :  Q((STATE_NUM_PHI+1)*(STATE_NUM_PHI_D+1)*(STATE_NUM_ACCEL + 1), std::vector<float>(ACTIONS,0)), 
     episode_num(0), time_steps(0), wins(0),
     loses(0), discount_factor(GAMMA), alpha(ALPHA),
     epsilon(EPSILON), pitch_dot(0.0), prev_pitch(0.0),
     reward_per_ep(0.0), pitch_dot_data(RUNNING_AVG, 0.0), running_avg_cntr(0)
{
q_state_publisher = n.advertise<controller::Q_state>("/Q_state", 1000);
if (MODEL_READ == 1){
  read_model();}

}



reinforcement_learning::~reinforcement_learning()
{
}

float reinforcement_learning::running_avg_pitch_dot(void)
{
  if (pitch_dot_data.size() < RUNNING_AVG){
  pitch_dot_data.push_back(pitch_dot);
  running_avg_cntr++;
  }else{  
   running_avg_cntr = RUNNING_AVG;
   pitch_dot_data.pop_front();
   pitch_dot_data.push_back(pitch_dot);
  }
  //for(int i = 0; i < RUNNING_AVG; i++)
   //{ ROS_INFO("%f", pitch_dot_data[i]);
//	}
 return (std::accumulate(pitch_dot_data.begin(), pitch_dot_data.end(), 0.0)) / running_avg_cntr;


}


void reinforcement_learning::Q_callback(const q_model_install::Q_state::ConstPtr& q_model)
{
//    Q[0] = q_model->state0;
//    Q[1] = q_model->state1;
	//std::cout<<*q_model<<std::endl;
       	for (int i = 0; i < ACTIONS; i++)
	{
	  Q[0][i] = q_model->state0[i]; 
	  Q[1][i] = q_model->state1[i]; 
	  Q[2][i] = q_model->state2[i]; 
	  Q[3][i] = q_model->state3[i]; 
	  Q[4][i] = q_model->state4[i]; 
	  Q[5][i] = q_model->state5[i]; 
	  Q[6][i] = q_model->state6[i]; 
	  Q[7][i] = q_model->state7[i]; 
	  Q[8][i] = q_model->state8[i]; 
	  Q[9][i] = q_model->state9[i]; 
	  Q[10][i] = q_model->state10[i]; 
	  Q[11][i] = q_model->state11[i]; 
	  Q[12][i] = q_model->state12[i]; 
	  Q[13][i] = q_model->state13[i]; 
	  Q[14][i] = q_model->state14[i]; 
	  Q[15][i] = q_model->state15[i]; 
	  Q[16][i] = q_model->state16[i]; 
	  Q[17][i] = q_model->state17[i]; 
	  Q[18][i] = q_model->state18[i]; 
	  Q[19][i] = q_model->state19[i]; 
	  Q[20][i] = q_model->state20[i]; 
	  Q[21][i] = q_model->state21[i]; 
	  Q[22][i] = q_model->state22[i]; 
	  Q[23][i] = q_model->state23[i]; 
	  Q[24][i] = q_model->state24[i]; 
	  Q[25][i] = q_model->state25[i]; 
	  Q[26][i] = q_model->state26[i]; 
	  Q[27][i] = q_model->state27[i]; 
	  Q[28][i] = q_model->state28[i]; 
	  Q[29][i] = q_model->state29[i]; 
	  Q[30][i] = q_model->state30[i]; 
	  Q[31][i] = q_model->state31[i]; 
	  Q[32][i] = q_model->state32[i]; 
	  Q[33][i] = q_model->state33[i]; 
	  Q[34][i] = q_model->state34[i]; 
	  Q[35][i] = q_model->state35[i]; 
	  Q[36][i] = q_model->state36[i]; 
	  Q[37][i] = q_model->state37[i]; 
	  Q[38][i] = q_model->state38[i]; 
	  Q[39][i] = q_model->state39[i]; 
	  Q[30][i] = q_model->state40[i]; 
	  Q[41][i] = q_model->state41[i]; 
	  Q[42][i] = q_model->state42[i]; 
	  Q[43][i] = q_model->state43[i]; 
	  Q[44][i] = q_model->state44[i]; 
	  Q[45][i] = q_model->state45[i]; 
	  Q[46][i] = q_model->state46[i]; 
	  Q[47][i] = q_model->state47[i]; 
	  Q[48][i] = q_model->state48[i]; 
	  Q[49][i] = q_model->state49[i]; 
	  Q[50][i] = q_model->state50[i]; 
	  Q[51][i] = q_model->state51[i]; 
	  Q[52][i] = q_model->state52[i]; 
	  Q[53][i] = q_model->state53[i]; 
	  Q[54][i] = q_model->state54[i]; 
	  Q[55][i] = q_model->state55[i]; 
	  Q[56][i] = q_model->state56[i]; 
	  Q[57][i] = q_model->state57[i]; 
	  Q[58][i] = q_model->state58[i]; 
	  Q[59][i] = q_model->state59[i]; 
	  Q[60][i] = q_model->state60[i]; 
	  Q[61][i] = q_model->state61[i]; 
	  Q[62][i] = q_model->state62[i]; 
	  Q[63][i] = q_model->state63[i]; 
	  Q[64][i] = q_model->state64[i]; 
	  Q[65][i] = q_model->state65[i]; 
	  Q[66][i] = q_model->state66[i]; 
	  Q[67][i] = q_model->state67[i]; 
	  Q[68][i] = q_model->state68[i]; 
	  Q[69][i] = q_model->state69[i];
	  Q[70][i] = q_model->state70[i]; 
	  Q[71][i] = q_model->state71[i]; 
	  Q[72][i] = q_model->state72[i]; 
	  Q[73][i] = q_model->state73[i]; 
	  Q[74][i] = q_model->state74[i]; 
	  Q[75][i] = q_model->state75[i]; 
	  Q[76][i] = q_model->state76[i]; 
	  Q[77][i] = q_model->state77[i]; 
	  Q[78][i] = q_model->state78[i]; 
	  Q[79][i] = q_model->state79[i]; 
	  Q[80][i] = q_model->state80[i]; 
	  Q[81][i] = q_model->state81[i]; 
	  Q[82][i] = q_model->state82[i]; 
	  Q[83][i] = q_model->state83[i]; 
	  Q[84][i] = q_model->state84[i]; 
	  Q[85][i] = q_model->state85[i]; 
	  Q[86][i] = q_model->state86[i]; 
	  Q[87][i] = q_model->state87[i]; 
	  Q[88][i] = q_model->state88[i]; 
	  Q[89][i] = q_model->state89[i];  
	  Q[90][i] = q_model->state90[i]; 
	  Q[91][i] = q_model->state91[i]; 
	  Q[92][i] = q_model->state92[i]; 
	  Q[93][i] = q_model->state93[i]; 
	  Q[94][i] = q_model->state94[i]; 
	  Q[95][i] = q_model->state95[i]; 
	  Q[96][i] = q_model->state96[i]; 
	  Q[97][i] = q_model->state97[i]; 
	  Q[98][i] = q_model->state98[i]; 
	  Q[99][i] = q_model->state99[i]; 
	  Q[100][i] = q_model->state100[i]; 
	  Q[101][i] = q_model->state101[i]; 
	  Q[102][i] = q_model->state102[i]; 
	  Q[103][i] = q_model->state103[i]; 
	  Q[104][i] = q_model->state104[i]; 
	  Q[105][i] = q_model->state105[i]; 
	  Q[106][i] = q_model->state106[i]; 
	  Q[107][i] = q_model->state107[i]; 
	  Q[108][i] = q_model->state108[i]; 
	  Q[109][i] = q_model->state109[i]; 
	  Q[110][i] = q_model->state110[i]; 
	  Q[111][i] = q_model->state111[i]; 
	  Q[112][i] = q_model->state112[i]; 
	  Q[113][i] = q_model->state113[i]; 
	  Q[114][i] = q_model->state114[i]; 
	  Q[115][i] = q_model->state115[i]; 
	  Q[116][i] = q_model->state116[i]; 
	  Q[117][i] = q_model->state117[i]; 
	  Q[118][i] = q_model->state118[i]; 
	  Q[119][i] = q_model->state119[i]; 


 	}


}

void reinforcement_learning::read_model(void)
{
	sub_q = n.subscribe("/q_installer", 1000, &reinforcement_learning::Q_callback, this);
}

float reinforcement_learning::get_reward(float pitch, float pitch_dot_imu)
{
  float squared_error_pitch = pow((pitch - REFERENCE_PITCH),2);
  float squared_error_pitch_dot = pow((pitch_dot_imu - 0), 2);
  
 /* if (std::abs(pitch) < std::abs(prev_pitch))
  {
    return (std::exp(pow(pitch - REFERENCE_PITCH,2)));
  }else{
  return (1 - std::exp(pow(pitch - REFERENCE_PITCH,2)));
  }
*/

 float reward_sgn;
  if (pitch_dot_imu < 0 && pitch < REFERENCE_PITCH){
    squared_error_pitch_dot = -squared_error_pitch_dot;}
  else if (pitch_dot_imu > 0 && pitch > REFERENCE_PITCH)
    {squared_error_pitch_dot = -squared_error_pitch_dot;
   }
  return (-squared_error_pitch + squared_error_pitch_dot);

 // return (1 - std::exp(pow(pitch - REFERENCE_PITCH,2)));



}
  /*if (std::abs(prev_pitch) < std::abs(pitch))
  {
    reward_sgn = -1.0;
  }else{
    reward_sgn = 1.0;
  }
  return (reward_sgn * squared_error_pitch);
  */

  /*
*/
/*
float reinforcement_learning::get_reward(float pitch, float pitch_dot_imu)
{
  float squared_error_pitch = pow((pitch - REFERENCE_PITCH),2);
  //float squared_error_pitch_dot = pow((pitch_dot_filtered - 0), 2);
  float squared_error_pitch_dot = pow((pitch_dot_imu - 0), 2);
  float sqrd_err_pitch_d;
  float reward;

//  return (1 - std::exp(pow(pitch - REFERENCE_PITCH,2)));

  if (pitch_dot > 0 && pitch < REFERENCE_PITCH)
  {
    sqrd_err_pitch_d = pow((pitch_dot - 0), 2);
    reward = -squared_error_pitch + sqrd_err_pitch_d;
  }else if (pitch_dot < 0 && pitch > REFERENCE_PITCH)
  {	
    sqrd_err_pitch_d = pow((pitch_dot - 0), 2);
    reward = -squared_error_pitch + sqrd_err_pitch_d;
  }else{
    sqrd_err_pitch_d = pow((pitch_dot_filtered - 0), 2);
    reward = -(squared_error_pitch + sqrd_err_pitch_d);
  }
  return reward;
}

  if (pitch_dot < 0 && pitch < REFERENCE_PITCH)
    squared_error_pitch_dot = -squared_error_pitch_dot;
  else if (pitch_dot > 0 && pitch > REFERENCE_PITCH)
    squared_error_pitch_dot = -squared_error_pitch_dot;

  return (-squared_error_pitch + squared_error_pitch_dot); 
}*/



char reinforcement_learning::choose_action(char)
{
}

void reinforcement_learning::TD_update(char curr_state, int action, char next_state, float reward)
{
  int max_action_idx;
  float td_target;
  float td_error;
  float Q_val;
  
  // get index value of Q next_state row with max reward value
  max_action_idx = distance(Q[next_state].begin(), max_element(Q[next_state].begin(), Q[next_state].end()));
  // compute update and write to Q at current state
  td_target = reward + discount_factor*Q[next_state][max_action_idx];
  td_error = td_target - Q[curr_state][action];
  Q[curr_state][action]+= td_error*alpha;
 
  // add more data 
  msg.max_action_idx = max_action_idx;
  msg.td_target = td_target;
  msg.td_error = td_error;
  msg.td_update = Q[curr_state][action];
  msg.alpha = alpha;
  msg.discount_factor = discount_factor;    
}


int reinforcement_learning::rpm_transform(void){
	float action_sng;
	if (action < 0){
	action_sng = -1.0;
	}else{
	action_sng = 1.0;
	}

	if (action == 0){
	return 0;
	}else{
	return int(action_sng*(-450.58*std::abs(action) +350));
	}
}


char reinforcement_learning::get_state(float pitch_, float pitch_dot_, float linear_accel_)
{
  int i, j,k;
  i = 0;
  j = 0;
  k = 0;

  for (int phi_idx = 0; phi_idx < STATE_NUM_PHI; phi_idx++)
  {
    ROS_INFO("vals %f", phi_states[phi_idx]);
    if (pitch_ <= phi_states[phi_idx])
    {
      i = phi_idx;
      break;
    }else if (pitch_ > phi_states[STATE_NUM_PHI - 1])
    {
      i = STATE_NUM_PHI;
      ROS_INFO("yooo %d", i);
      break;
     }
   }
   ROS_INFO("phi bound: %f and index: %d", phi_states[i], i);

/*  if (std::abs(pitch) > 7.5)
  {
      for (char phi_d_idx = 0; phi_d_idx < STATE_NUM_PHI_D; phi_d_idx++)
      {
      if (pitch_dot <= phi_d_states_high_vel[phi_d_idx])
       {
        j = phi_d_idx;
        break;
       }else if (pitch_dot > phi_d_states_high_vel[STATE_NUM_PHI_D - 1])
        {
         j = STATE_NUM_PHI_D;
         break;
        }
      }
  }else{
*/
  for (int phi_d_idx = 0; phi_d_idx < STATE_NUM_PHI_D; phi_d_idx++)
  {
    if (pitch_dot_ <= phi_d_states[phi_d_idx])
    {
      j = phi_d_idx;
      break;
    }else if (pitch_dot_ > phi_d_states[STATE_NUM_PHI_D - 1])
    {
      j = STATE_NUM_PHI_D;
      break;
    }
 }

  for (int accel_idx = 0; accel_idx < STATE_NUM_ACCEL; accel_idx++)
  {
    if (linear_accel_ <= linear_accel_states[accel_idx])
   {
    k = accel_idx;
    break;
   }
  else if (linear_accel_ > linear_accel_states[STATE_NUM_ACCEL - 1])
   {
    k = STATE_NUM_ACCEL;
    break;
   }
  }
  std::cout<<"i:"<<i<<"j:"<<j<<"k:"<<k<<std::endl;
  return (k + (j * STATE_NUM_PHI_D) + (i * STATE_NUM_PHI));

  //return( j + (STATE_NUM_PHI_D + 1) * i);
}




void reinforcement_learning::publishQstate()
{
  controller::Q_state q_msg;

  for (int i = 0; i < ACTIONS; i++)
  {
    q_msg.state0[i] = Q[0][i];
    q_msg.state1[i] = Q[1][i];
    q_msg.state2[i] = Q[2][i];
    q_msg.state3[i] =  Q[3][i];
    q_msg.state4[i] =  Q[4][i];
    q_msg.state5[i] =  Q[5][i];
    q_msg.state6[i] =  Q[6][i];
    q_msg.state7[i] =  Q[7][i];
    q_msg.state8[i] =  Q[8][i];
    q_msg.state9[i] =  Q[9][i];
    q_msg.state10[i] =  Q[10][i];
    q_msg.state11[i] =  Q[11][i];
    q_msg.state12[i] =  Q[12][i];
    q_msg.state13[i] =  Q[13][i];
    q_msg.state14[i] =  Q[14][i];
    q_msg.state15[i] =  Q[15][i];
    q_msg.state16[i] =  Q[16][i];
    q_msg.state17[i] =  Q[17][i];
    q_msg.state18[i] =  Q[18][i];
    q_msg.state19[i] =  Q[19][i];
    q_msg.state20[i] =  Q[20][i];
    q_msg.state21[i] =  Q[21][i];
    q_msg.state22[i] =  Q[22][i];
    q_msg.state23[i] =  Q[23][i];
    q_msg.state24[i] =  Q[24][i];
    q_msg.state25[i] =  Q[25][i];
    q_msg.state26[i] =  Q[26][i];
    q_msg.state27[i] =  Q[27][i];
    q_msg.state28[i] =  Q[28][i];
    q_msg.state29[i] =  Q[29][i];
    q_msg.state30[i] =  Q[30][i];
    q_msg.state31[i] =  Q[31][i];
    q_msg.state32[i] =  Q[32][i];
    q_msg.state33[i] =  Q[33][i];
    q_msg.state34[i] =  Q[34][i];
    q_msg.state35[i] =  Q[35][i];
    q_msg.state36[i] =  Q[36][i];
    q_msg.state37[i] =  Q[37][i];
    q_msg.state38[i] =  Q[38][i];
    q_msg.state39[i] =  Q[39][i];
    q_msg.state40[i] =  Q[40][i];
    q_msg.state41[i] =  Q[41][i];
    q_msg.state42[i] =  Q[42][i];
    q_msg.state43[i] =  Q[43][i];
    q_msg.state44[i] =  Q[44][i];
    q_msg.state45[i] =  Q[45][i];
    q_msg.state46[i] =  Q[46][i];
    q_msg.state47[i] =  Q[47][i];
    q_msg.state48[i] =  Q[48][i];
    q_msg.state49[i] =  Q[49][i];
    q_msg.state50[i] =  Q[50][i];
    q_msg.state51[i] =  Q[51][i];
    q_msg.state52[i] =  Q[52][i];
    q_msg.state53[i] =  Q[53][i];
    q_msg.state54[i] =  Q[54][i];
    q_msg.state55[i] =  Q[55][i];
    q_msg.state56[i] =  Q[56][i];
    q_msg.state57[i] =  Q[57][i];
    q_msg.state58[i] =  Q[58][i];
    q_msg.state59[i] =  Q[59][i];
    q_msg.state60[i] =  Q[60][i];
    q_msg.state61[i] =  Q[61][i];
    q_msg.state62[i] =  Q[62][i];
    q_msg.state63[i] =  Q[63][i];
    q_msg.state64[i] =  Q[64][i];
    q_msg.state65[i] =  Q[65][i];
    q_msg.state66[i] =  Q[66][i];
    q_msg.state67[i] =  Q[67][i];
    q_msg.state68[i] =  Q[68][i];
    q_msg.state69[i] =  Q[69][i];
    q_msg.state70[i] =  Q[70][i];
    q_msg.state71[i] =  Q[71][i];
    q_msg.state72[i] =  Q[72][i];
    q_msg.state73[i] =  Q[73][i];
    q_msg.state74[i] =  Q[74][i];
    q_msg.state75[i] =  Q[75][i];
    q_msg.state76[i] =  Q[76][i];
    q_msg.state77[i] =  Q[77][i];
    q_msg.state78[i] =  Q[78][i];
    q_msg.state79[i] =  Q[79][i];
    q_msg.state80[i] =  Q[80][i];
    q_msg.state81[i] =  Q[81][i];
    q_msg.state82[i] =  Q[82][i];
    q_msg.state83[i] =  Q[83][i];
    q_msg.state84[i] =  Q[84][i];
    q_msg.state85[i] =  Q[85][i];
    q_msg.state86[i] =  Q[86][i];
    q_msg.state87[i] =  Q[87][i];
    q_msg.state88[i] =  Q[88][i];
    q_msg.state89[i] =  Q[89][i];
    q_msg.state90[i] =  Q[90][i];
    q_msg.state91[i] =  Q[91][i];
    q_msg.state92[i] =  Q[92][i];
    q_msg.state93[i] =  Q[93][i];
    q_msg.state94[i] =  Q[94][i];
    q_msg.state95[i] =  Q[95][i];
    q_msg.state96[i] =  Q[96][i];
    q_msg.state97[i] =  Q[97][i];
    q_msg.state98[i] =  Q[98][i];
    q_msg.state99[i] =  Q[99][i];
    q_msg.state100[i] =  Q[100][i];
    q_msg.state101[i] =  Q[101][i];
    q_msg.state102[i] =  Q[102][i];
    q_msg.state103[i] =  Q[103][i];
    q_msg.state104[i] =  Q[104][i];
    q_msg.state105[i] =  Q[105][i];
    q_msg.state106[i] =  Q[106][i];
    q_msg.state107[i] =  Q[107][i];
    q_msg.state108[i] =  Q[108][i];
    q_msg.state109[i] =  Q[109][i];
    q_msg.state110[i] =  Q[110][i];
    q_msg.state111[i] =  Q[111][i];
    q_msg.state112[i] =  Q[112][i];
    q_msg.state113[i] =  Q[113][i];
    q_msg.state114[i] =  Q[114][i];
    q_msg.state115[i] =  Q[115][i];
    q_msg.state116[i] =  Q[116][i];
    q_msg.state117[i] =  Q[117][i];
    q_msg.state118[i] =  Q[118][i];
    q_msg.state119[i] =  Q[119][i];
 } 
// publish important data
  q_state_publisher.publish(q_msg);

}


class q_learning: public Controller, public reinforcement_learning
{
  public:
    q_learning();
    ~q_learning();
    std::vector<float> q_row;
    std::vector<int> max_value_idxs;
    
    char choose_action(char);
    void take_action(int);
};

q_learning::q_learning()
{
    this->q_row.resize(6);
} 
 
q_learning::~q_learning()
{
}

char q_learning::choose_action(char curr_state)
{
  int random_choice;
  float random_num;
  float max_q;
  int action_choice;
  char position_bias;
  char position_lower_bound;
  char position_upper_bound;

  // generate random number to decide whether to explore or exploit
  random_num = fabs((rand()/(float)(RAND_MAX)));	//random num between 0 and 1
  ROS_INFO("random num: %f", random_num);
  msg.action_choice = random_num;

  this->q_row = Q[curr_state];
  std::vector<float> q_row_final;  
  //pitch position bias
  if (pitch >= 0)
  {

    position_bias = ACTION_BIAS;
    position_lower_bound = ACTION_BIAS;
    position_upper_bound = ACTIONS;
  }else{
   
  position_bias = 0;
  position_lower_bound = 0;
  position_upper_bound = ACTION_BIAS + 1;
 
  }

 
  if (random_num < epsilon)
  {
    //pick randomly
    random_choice = rand()%(ACTIONS_HALF) + position_bias;
    ROS_INFO("random action choice: %d", random_choice);
    ROS_INFO("position bias %d", position_bias);
    msg.random_action = random_choice;
    return random_choice;
  }
  else
  {
    //pick best
    msg.random_action = 50;
    ROS_INFO("picks best");
    this->q_row = Q[curr_state];
    ROS_INFO("position lower bound: %d", position_lower_bound);
    ROS_INFO("position upper bound: %d", position_upper_bound);
    std::vector<float>::const_iterator first  = q_row.begin() + position_lower_bound;
    std::vector<float>::const_iterator end  = q_row.begin() + position_upper_bound;
    std::vector<float> q_row_final(first, end);
    //std::cout<<q_row_final<<std::endl;

    ROS_INFO("1");
    


    for (int i = 0; i < ACTIONS; i ++)
    {
      ROS_INFO("q row element %f", q_row[i]);
    }


    for (int i = 0; i < 4; i ++)
    {
      ROS_INFO("q row bouned element %f", q_row_final[i]);
    }



    max_q = *std::max_element(q_row_final.begin(), q_row_final.end());
    ROS_INFO("max_q from q row bounded: %f", max_q);    
    for (int i = 0; i < q_row_final.size(); i++)
      {
        if (max_q == q_row_final[i])
          {
            max_value_idxs.push_back(i);     
	  }
      }

    if (max_value_idxs.size() == 1)
      {
	action_choice = max_value_idxs[0] + position_bias;
	//std::fill(max_value_idxs.being(), max_value_idxs.end(),0);
        ROS_INFO("selected action when only one max indx");
 	max_value_idxs.clear();
	return action_choice;
      }
	else if (max_value_idxs.size() > 1)
      { 
	ROS_INFO("max_value_dx when multiple max indx is: %d", max_value_idxs[0] + position_bias);
	action_choice = max_value_idxs[0] + position_bias;
	max_value_idxs.clear();
//	ROS_INFO("random choice for repeated bests");
//	ROS_INFO("size: %d", max_value_idxs.size());
//        action_choice = rand()%(max_value_idxs.size());
//	max_value_idxs.clear();
//	ROS_INFO("rnadom choice is: %d", action_choice);
        return action_choice;
      }
  }
}


int main(int argc, char **argv)
{
	ros::init(argc, argv, "control");

	ros::NodeHandle n;
	ros::Rate loop_rate(FREQUENCY); 

	ros::Publisher pwm_command = n.advertise<std_msgs::Int16>("/pwm_cmd", 1000);
	ros::Publisher state_publisher = n.advertise<controller::State>("/State", 1000);

	char state;
	float reward;
    	std_msgs::Int16 pwm_msg;
	double restart_delta_prev, restart_delta, epsilon_delta_prev, epsilon_delta;

	q_learning controller;
	


	while (ros::ok())
	{
	  ros::spinOnce(); //update pitch
	 controller.msg.pitch = controller.pitch;
  	  controller.msg.pitch_dot = controller.pitch_dot;
  	  controller.msg.pitch_dot_imu = controller.pitch_dot_imu;	


	 
	  if(std::abs(controller.pitch) < 0.5)
	  {
	  	controller.motors = true;
	  }

	  //next episode
	  if (std::abs(controller.pitch) > PITCH_THRESHOLD)
		{
		  restart_delta = ros::Time::now().toSec();
		  if (restart_delta - restart_delta_prev > 0.5)
			{

			  ROS_INFO("RESTART SIM - pitch is: %f!", controller.pitch);
			  controller.episode_num++;
			  controller.msg.episodes = controller.episode_num;
			  ROS_INFO("EPISODE COUNT: %d", controller.episode_num);
			  //initalise appropriate variables
			  controller.time_steps = 0;
			  controller.prev_pitch = 0;
			  controller.pitch_dot = 0;
			  controller.reward_per_ep = 0;

			  //clear message
			  controller.msg.pitch = 0;
			  controller.msg.pitch_dot = 0;
			  controller.msg.action = 0;
			  controller.msg.action_idx = 0;
			  controller.msg.current_state = 0;
			  controller.msg.next_state = 0;
			  controller.msg.td_update = 0;
			  controller.msg.td_target = 0;
			  controller.msg.td_error = 0;
			

			}
			restart_delta_prev = restart_delta;
	   	        pwm_msg.data = STOP_TORQUE;//STOP_PWM;
		        pwm_command.publish(pwm_msg);
			controller.motors = false;
			controller.pitch_dot_data.clear();
		/*	for (int i = 0; i < RUNNING_AVG; i++)
			{
			  controller.pitch_dot_data[i] = 0;
			}*/
			controller.running_avg_cntr = 0;
			//ROS_INFO("cntr contr %d", controller.running_avg_cntr);
		}

		  // apply control if segway is still in pitch range
		  if (std::abs(controller.pitch) <= PITCH_THRESHOLD && controller.motors == true)
		  {
			// class member variables
			controller.pitch_dot = (controller.pitch - controller.prev_pitch)/RL_DELTA;

			// message member variables
			controller.msg.pitch = controller.pitch;
			controller.msg.pitch_dot = controller.pitch_dot;
			controller.msg.epsilon = controller.epsilon;
			controller.msg.time_steps = controller.time_steps;
			controller.msg.error = controller.pitch - REFERENCE_PITCH;
			controller.msg.prev_pitch = controller.prev_pitch;
			controller.msg.pitch_dot_imu = controller.pitch_dot_imu;	

			// get state of the system
			controller.pitch_dot_filtered = controller.running_avg_pitch_dot();
			controller.msg.pitch_dot_filtered = controller.pitch_dot_filtered;
//			state = controller.get_state(controller.pitch, controller.pitch_dot_filtered);
			state = controller.get_state(controller.pitch, controller.pitch_dot_imu, controller.linear_accel);


			// debugging data:	
			ROS_INFO("episode: %d", controller.episode_num);
			ROS_INFO("time step: %d", controller.time_steps);	
			ROS_INFO("pitch: %f", controller.pitch);
			ROS_INFO("pitch prev: %f", controller.prev_pitch);
		 	ROS_INFO("pitch dot: %f", controller.pitch_dot);
			ROS_INFO("pitch dot filtered %f", controller.pitch_dot_filtered);
			ROS_INFO("pitch dot imu  %f", controller.pitch_dot_imu);
			ROS_INFO("linear accel imu %f",controller.linear_accel);

			ROS_INFO("state: %d", state);
			controller.msg.linear_accel = controller.linear_accel;

			// first iteration
			if (controller.time_steps < 1)
			{
		   	  // set the current state
		  	  controller.current_state = state;
			  ROS_INFO("current state: %d", controller.current_state);

				  // select action
			  controller.action_idx = controller.choose_action(controller.current_state);
			  controller.action = actions[controller.action_idx];
			  ROS_INFO("action idx %d and action: %f", controller.action_idx, controller.action);	
			 
			 // take action (ie. publish action)
			 // pwm_msg.data = controller.rpm_transform();
			pwm_msg.data = controller.action;  
			pwm_command.publish(pwm_msg);



			}else if (controller.time_steps >= 1)
			{
		   	  // set the  next state 
			  controller.next_state = state;
			  controller.msg.next_state = controller.next_state;
			  ROS_INFO("next state: %d", controller.next_state);
			  
			  // get reward
			  reward = controller.get_reward(controller.pitch, controller.pitch_dot_imu);
			  controller.msg.reward = reward;
			  ROS_INFO("reward is %f", reward);	

			  controller.reward_per_ep+=reward;
			  controller.msg.reward_per_ep = controller.reward_per_ep;

			  //TD update
			  controller.TD_update(controller.current_state, controller.action_idx, controller.next_state, reward);
			  
			  // publish Q(s,a) matrix
			  controller.publishQstate();	
			  // publish state data
		  	  state_publisher.publish(controller.msg);
	
			  // Now move from the next state to the current state
			  controller.current_state = controller.next_state;
		 
			  // select action
			  controller.action_idx = controller.choose_action(controller.current_state);
			  controller.action = actions[controller.action_idx];
			  ROS_INFO("action idx %d and action: %f", controller.action_idx, controller.action);	
			  
			  // take action (ie. publish action)
			//  pwm_msg.data = controller.rpm_transform();
			 // ROS_INFO("action in RPMS is: %d" ,pwm_msg.data);
			  pwm_msg.data = controller.action;
			  pwm_command.publish(pwm_msg);

		 	}
		 
			// set prev pitch
			controller.prev_pitch = controller.pitch;

			// add data to message (state, action, action_idx,  to msg
			controller.msg.current_state = controller.current_state;
			controller.msg.action = controller.action;
			controller.msg.action_idx = controller.action_idx;

			//increment timestep
			controller.time_steps++;

			//decrease epsilon every 20 eps
			if (controller.episode_num % 15 == 0 && controller.episode_num > 1)
			{
			  epsilon_delta = ros::Time::now().toSec();
			  if (epsilon_delta - epsilon_delta_prev > 0.1)
			  {
				ROS_INFO("DECREASE PARAMS");
				controller.epsilon = controller.epsilon/2;
			    if (controller.epsilon < 0.1){
				  controller.epsilon = 0;
				}
		  	   controller.msg.epsilon = controller.epsilon;
				
			  }
			  epsilon_delta_prev = epsilon_delta;
			}

			if (controller.episode_num == MAX_EPISODE)
			{
			  ROS_INFO("SIMULATION COMPLETE AT %d EPISODES", controller.episode_num);
			  while(1){}
			}
		}	

			loop_rate.sleep();
	}
	return 0;
}