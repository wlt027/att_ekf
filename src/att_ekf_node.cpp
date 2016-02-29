#include <iostream>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/MagneticField.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Vector3Stamped.h>

#include <Eigen/Eigen>
#include "att_ekf.h"
#include "conversion.h"
#include <deque>


using namespace std;
using namespace Eigen;

Att_ekf att_ekf;
deque<pair<double, geometry_msgs::Vector3Stamped> > mag_q;
deque<pair<double, sensor_msgs::Imu> >imu_q;


ros::Subscriber imu_bias_sub;
bool imu_bias_initialized = false;
Vector3d gyro_bias;
Vector3d acc_bias;

Quaterniond q_gt;
double t_gt;
ros::Publisher pose_pub, pose_gt_pub;

void magCallback(const geometry_msgs::Vector3StampedConstPtr& msg)
{
	geometry_msgs::Vector3Stamped mag_msg = *msg;
	double t = msg->header.stamp.toSec();
	mag_q.push_back(make_pair(t, mag_msg));
	// Vector3d mag;
	// mag(0) = msg->vector.x;
	// mag(1) = msg->vector.y;
	// mag(2) = msg->vector.z;
	//cout << "mag: " << mag.transpose() << endl;
}


void imuCallback(const sensor_msgs::ImuConstPtr & msg)
{
	double t = msg->header.stamp.toSec();
	sensor_msgs::Imu imu_msg = *msg;
	imu_q.push_back(make_pair(t, imu_msg));

	// Quaterniond q;
	q_gt.w() = msg->orientation.w;
	q_gt.x() = msg->orientation.x;
	q_gt.y() = msg->orientation.y;
	q_gt.z() = msg->orientation.z;
	t_gt = msg->header.stamp.toSec();

	geometry_msgs::PoseStamped pose_gt;
	pose_gt.header.stamp = ros::Time(t_gt);
	pose_gt.header.frame_id = "/world";
 	pose_gt.pose.orientation.w = q_gt.w();
 	pose_gt.pose.orientation.x = q_gt.x();
 	pose_gt.pose.orientation.y = q_gt.y();
 	pose_gt.pose.orientation.z = q_gt.z();
 	pose_gt_pub.publish(pose_gt);
	//cout << "q_gt: " << q_gt.w() << " " << q_gt.vec().transpose() << endl;
}

void imuBiasCallback(const sensor_msgs::ImuConstPtr & msg)
{
	acc_bias(0) = msg->linear_acceleration.x;
	acc_bias(1) = msg->linear_acceleration.y;
	acc_bias(2) = msg->linear_acceleration.z;
	gyro_bias(0) = msg->angular_velocity.x;
	gyro_bias(1) = msg->angular_velocity.y;
	gyro_bias(2) = msg->angular_velocity.z;
	imu_bias_initialized = true;
	imu_bias_sub.shutdown();
}

void publish_pose()
{
	geometry_msgs::PoseStamped pose;
	MatrixXd m = att_ekf.get_rotation_matrix();
 	Quaterniond q = mat2quaternion(m);
 	pose.header.stamp = ros::Time(att_ekf.get_time());
 	pose.header.frame_id = "/world";
 	pose.pose.orientation.w = q.w();
 	pose.pose.orientation.x = q.x();
 	pose.pose.orientation.y = q.y();
 	pose.pose.orientation.z = q.z();
 	pose_pub.publish(pose);
}

int main(int argc, char **argv)
{
	ros::init(argc, argv, "laser_localization");
	ros::NodeHandle n("~");
	ros::Subscriber imu_sub = n.subscribe("/imu", 100, imuCallback);
	imu_bias_sub = n.subscribe("/imu_bias", 100, imuBiasCallback);
	ros::Subscriber mag_sub = n.subscribe("/magnetic_field", 100, magCallback);
	pose_pub = n.advertise<geometry_msgs::PoseStamped>("/pose", 10);
	pose_gt_pub = n.advertise<geometry_msgs::PoseStamped>("/pose_gt", 10);
	ros::Rate loop_rate(100);
	while(ros::ok())
	{
		ros::spinOnce();
		
		if(imu_q.size() == 0 || mag_q.size() == 0) continue;

		while(imu_q.back().first - imu_q.front().first > 0.05)//buffer size
		{
			if(imu_q.front().first < mag_q.front().first)
			{
				Vector3d acc, gyro;
				acc(0) = imu_q.front().second.linear_acceleration.x;
				acc(1) = imu_q.front().second.linear_acceleration.y;
				acc(2) = imu_q.front().second.linear_acceleration.z;
				gyro(0) = imu_q.front().second.angular_velocity.x;
				gyro(1) = imu_q.front().second.angular_velocity.y;
				gyro(2) = imu_q.front().second.angular_velocity.z;
				double t = imu_q.front().first;
				if(imu_bias_initialized) 
				{
					acc -= acc_bias;
					gyro -= gyro_bias;
				}
				att_ekf.update_imu(acc, gyro, t);
				publish_pose();
				imu_q.pop_front();
			}else 
			{
				Vector3d mag;
				mag(0) = mag_q.front().second.vector.x;
				mag(1) = mag_q.front().second.vector.y;
				mag(2) = mag_q.front().second.vector.z;
				double t = mag_q.front().first;
				att_ekf.update_magnetic(mag, t);
				publish_pose();
				mag_q.pop_front();
			}
			if(imu_q.size() == 0 || mag_q.size() == 0) break;
		}
		// cout << "t: " << att_ekf.get_time() << endl;
		// Quaterniond q = mat2quaternion(att_ekf.get_rotation_matrix());
		// cout << "q: " << q.w() << " " << q.vec().transpose() << endl;
		// cout << "q_gt: " << q_gt.w() << " " << q_gt.vec().transpose() << endl;

		
		//Quaterniond dq = q.inverse()*q_gt;
		//cout << "dq: " << dq.w() << " " << dq.vec().transpose() << endl;
		loop_rate.sleep();
	}
	ros::spin();

	return 0;
}