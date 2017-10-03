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

#include "robot_teleop_tuner/pid_values.h"


#include "controller/PidData.h"
#include <std_msgs/Int16.h>

#define FREQUENCY 25
#define PID_DELTA 0.04
#define BAUD_RATE 115200

#define PROP_GAIN 0
#define INT_GAIN 0
#define DERIV_GAIN 0

#define STOP_PWM 130

#define PITCH_FIX 0
#define REFERENCE_PITCH 0

namespace patch
{
	template < typename T > 
	std::string to_string(const T& t)
	{
		std::ostringstream stm;
		stm << t;
		return stm.str();
	}
}



class Controller
{

	public:
		Controller();
		~Controller();
		void init();
		void write_serial_command(std::string const& command);
		std::string motor_cmd_generator(int);
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

		//Other
		int episodes;
		int time_steps;
};



Controller::Controller()
	:	roll(0.0)
	    ,	pitch(0.0)
	    ,	yaw(0.0)
	    ,   episodes(0)
	    ,   time_steps(0)
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
	pitch = pitch*(180/M_PI) + PITCH_FIX;
}		

std::string Controller::motor_cmd_generator(int cmd)
{
	std::string m1, m2, result;
	int cmd_abs = std::abs(cmd);
	if (cmd_abs >= 100)
	{	
		m1 = patch::to_string(cmd_abs);
//TODO: boost vs string stream...who preforms best?!		
//		m1 = boost::lexical_cast<std::string>(cmd);
		m2 = m1;
	} else if (cmd_abs >= 10)
	{
		m1 = "0" + patch::to_string(cmd_abs);
		m2 = m1;
	} else if (cmd_abs >= 0)
	{
		m1 = "00" + patch::to_string(cmd_abs);
		m2 = m1;
	}
	if (cmd > 0)
	{
		result = "+" + m1 + "+" + m2;
	}else
	{
		result = "-" + m1 + "-" + m2;
	}
	return result;

}

void Controller::write_serial_command(std::string const& command)
{
	fflush(stdout);
	serialPuts(fd, command.c_str());	
}

void Controller::pitch_tolerance()
{
	if (std::abs(pitch) < 0.5)
	{
		pitch = 0.0;
	}
}

class PID : public Controller
{
	public:
	float kp;
	float ki;
	float kd;
	float pitch_ref;
	float error_prev;
	float integral_sum;

	controller::PidData msg;
	ros::Subscriber sub_pid;
	PID(float, float, float);
	~PID();
	float updatePID();
	void init();
	int saturate(int);
	void pid_callback(const robot_teleop_tuner::pid_values::ConstPtr& pid_input);
	float saturateIntSum(float);
	void checkReset(void);
};

PID::PID(float kp_, float ki_, float kd_):
	pitch_ref(0)
{
	kp = kp_;
	ki = ki_;
	kd = kd_;
	this->init();
	ROS_INFO("pid instance created");
	sub_pid = n.subscribe("/pid_tuner", 1000, &PID::pid_callback, this);
	
}

void PID::pid_callback(const robot_teleop_tuner::pid_values::ConstPtr& pid_input)
{
    kp = pid_input->p;
    ki = pid_input->i;
    kd = pid_input->d;
}

PID::~PID()
{
	ROS_INFO("PID node destroyed");
}

void PID::init()
{
	error_prev = 0;
	integral_sum = 0;
}


void PID::checkReset()
{
	if (std::abs(pitch) < 0.3)
	{
	  this->init();
	}
}

float PID::updatePID()
{
	float error, error_kp, error_ki, error_kd, derivative, pid_cmd;
	
	error = pitch - REFERENCE_PITCH;//pitch_ref;//pitch_ref - pitch;
	msg.error = error;

	error_kp = error * kp;
	msg.error_proportional = error_kp;

	integral_sum += error * PID_DELTA;
	integral_sum = saturateIntSum(integral_sum);
	error_ki = integral_sum * ki;
	msg.integral_sum = integral_sum;
	msg.error_integral = error_ki;

	derivative = (error - error_prev)/PID_DELTA;
	error_kd = derivative * kd;
	msg.derivative = derivative;
	msg.error_derivative = error_kd;
	msg.error_prev = error_prev;
	error_prev = error;
	
	pid_cmd = (error_kp + error_ki + error_kd);
	msg.pid_cmd = pid_cmd;	
	return pid_cmd;
}

int PID::saturate(int pid_cmd)
{
	if (pid_cmd >= 255)
	{
		pid_cmd = 255;
	}else if (pid_cmd <= 1)
	{
		pid_cmd = 1;
	}
	return pid_cmd;
}

float PID::saturateIntSum(float integral_sum)
{
	if (integral_sum > 50)
	{
	  integral_sum = 50;
	}
	else if (integral_sum < -50)
	{
	  integral_sum = -50;
	}
	return integral_sum;
}
int main(int argc, char **argv)
{
	ros::init(argc, argv, "control");

	ros::NodeHandle n;
	ros::Rate loop_rate(FREQUENCY); 

	ros::Publisher pid_data = n.advertise<controller::PidData>("/pid_data", 1000);
	ros::Publisher pwm_command = n.advertise<std_msgs::Int16>("/pwm_cmd", 1000);

	PID pid(PROP_GAIN,INT_GAIN,DERIV_GAIN);

	std::string command;
        std_msgs::Int16 pwm_msg;
	
	int pid_cmd;

	while (ros::ok())
	{
		ros::spinOnce(); //update pitch

		pid.time_steps++;
		pid.msg.time_steps = pid.time_steps;

		ROS_INFO("%f", pid.pitch);
		//pid.pitch_tolerance();
		//ROS_INFO("pitch after tolerance: %f", pid.pitch);	
		pid.checkReset();

	//	pid_cmd = static_cast<int>(round(pid.updatePID()))/256 + 128;
		pid_cmd = pid.updatePID() + STOP_PWM;
		pid_cmd = pid.saturate(pid_cmd);
		//ROS_INFO("pid cmd: %d", pid_cmd);
		pid.msg.motor_cmd = pid_cmd;	
	//	command = pid.motor_cmd_generator(pid_cmd);
	//	std::cout<<"motor cmd: "<<command<<std::endl;
		pwm_msg.data = pid_cmd;
		pwm_command.publish(pwm_msg);
	//	pid.write_serial_command(command);

		pid_data.publish(pid.msg);

		loop_rate.sleep();
	}
	return 0;
}